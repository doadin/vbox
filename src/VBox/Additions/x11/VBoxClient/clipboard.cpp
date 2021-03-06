/** $Id$ */
/** @file
 * Guest Additions - X11 Shared Clipboard.
 */

/*
 * Copyright (C) 2007-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>

#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>
#include <VBox/GuestHost/SharedClipboard.h>

#include "VBoxClient.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * Global clipboard context information.
 */
struct _SHCLCONTEXT
{
    /** Client command context */
    VBGLR3SHCLCMDCTX CmdCtx;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    /** Associated transfer data. */
    SHCLTRANSFERCTX  TransferCtx;
#endif
    /** Pointer to the X11 clipboard backend. */
    CLIPBACKEND     *pBackend;
};

/** Only one client is supported. There seems to be no need for more clients. */
static SHCLCONTEXT g_Ctx;


/**
 * Get clipboard data from the host.
 *
 * @returns VBox result code
 * @param   pCtx                Our context information.
 * @param   Format              The format of the data being requested.
 * @param   ppv                 On success and if pcb > 0, this will point to a buffer
 *                              to be freed with RTMemFree containing the data read.
 * @param   pcb                 On success, this contains the number of bytes of data
 *                              returned.
 */
DECLCALLBACK(int) ClipRequestDataForX11Callback(SHCLCONTEXT *pCtx, SHCLFORMAT Format, void **ppv, uint32_t *pcb)
{
    RT_NOREF(pCtx);

    LogFlowFunc(("Format=0x%x\n", Format));

    int rc = VINF_SUCCESS;

    uint32_t cbRead = 0;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    if (Format == VBOX_SHCL_FMT_URI_LIST)
    {
        //rc = VbglR3ClipboardRootListRead()
    }
    else
#endif
    {
        SHCLDATABLOCK dataBlock;
        RT_ZERO(dataBlock);

        dataBlock.uFormat = Format;
        dataBlock.cbData  = _4K;
        dataBlock.pvData  = RTMemAlloc(dataBlock.cbData);
        if (dataBlock.pvData)
        {
            rc = VbglR3ClipboardReadDataEx(&pCtx->CmdCtx, &dataBlock, &cbRead);
        }
        else
            rc = VERR_NO_MEMORY;

        /*
         * A return value of VINF_BUFFER_OVERFLOW tells us to try again with a
         * larger buffer.  The size of the buffer needed is placed in *pcb.
         * So we start all over again.
         */
        if (rc == VINF_BUFFER_OVERFLOW)
        {
            /* cbRead contains the size required. */

            dataBlock.cbData = cbRead;
            dataBlock.pvData = RTMemRealloc(dataBlock.pvData, cbRead);
            if (dataBlock.pvData)
            {
                rc = VbglR3ClipboardReadDataEx(&pCtx->CmdCtx, &dataBlock, &cbRead);
                if (rc == VINF_BUFFER_OVERFLOW)
                    rc = VERR_BUFFER_OVERFLOW;
            }
            else
                rc = VERR_NO_MEMORY;
        }

        if (RT_SUCCESS(rc))
        {
            *pcb = cbRead; /* Actual bytes read. */
            *ppv = dataBlock.pvData;
        }

        /*
         * Catch other errors. This also catches the case in which the buffer was
         * too small a second time, possibly because the clipboard contents
         * changed half-way through the operation.  Since we can't say whether or
         * not this is actually an error, we just return size 0.
         */
        if (RT_FAILURE(rc))
            RTMemFree(dataBlock.pvData);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Opaque data structure describing a request from the host for clipboard
 * data, passed in when the request is forwarded to the X11 backend so that
 * it can be completed correctly.
 */
struct _CLIPREADCBREQ
{
    /** The data format that was requested. */
    SHCLFORMAT Format;
};

/**
 * Tell the host that new clipboard formats are available.
 *
 * @param pCtx                  Our context information.
 * @param Formats               The formats to report.
 */
DECLCALLBACK(void) ClipReportX11FormatsCallback(SHCLCONTEXT *pCtx, SHCLFORMATS Formats)
{
    RT_NOREF(pCtx);

    LogFlowFunc(("Formats=0x%x\n", Formats));

    SHCLFORMATDATA formatData;
    RT_ZERO(formatData);

    formatData.Formats = Formats;

    int rc2 = VbglR3ClipboardFormatsReportEx(&pCtx->CmdCtx, &formatData);
    RT_NOREF(rc2);
    LogFlowFuncLeaveRC(rc2);
}

/**
 * This is called by the backend to tell us that a request for data from
 * X11 has completed.
 *
 * @param  pCtx                 Our context information.
 * @param  rc                   The IPRT result code of the request.
 * @param  pReq                 The request structure that we passed in when we started
 *                              the request.  We RTMemFree() this in this function.
 * @param  pv                   The clipboard data returned from X11 if the request succeeded (see @a rc).
 * @param  cb                   The size of the data in @a pv.
 */
DECLCALLBACK(void) ClipRequestFromX11CompleteCallback(SHCLCONTEXT *pCtx, int rc, CLIPREADCBREQ *pReq, void *pv, uint32_t cb)
{
    RT_NOREF(pCtx);

    LogFlowFunc(("rc=%Rrc, Format=0x%x, pv=%p, cb=%RU32\n", rc, pReq->Format, pv, cb));

    SHCLDATABLOCK dataBlock;
    RT_ZERO(dataBlock);

    dataBlock.uFormat = pReq->Format;

    if (RT_SUCCESS(rc))
    {
        dataBlock.pvData = pv;
        dataBlock.cbData = cb;
    }

    int rc2 = VbglR3ClipboardWriteDataEx(&pCtx->CmdCtx, &dataBlock);
    RT_NOREF(rc2);

    RTMemFree(pReq);

    LogFlowFuncLeaveRC(rc2);
}

/**
 * Connect the guest clipboard to the host.
 *
 * @returns VBox status code.
 */
static int vboxClipboardConnect(void)
{
    LogFlowFuncEnter();

    int rc;

    g_Ctx.pBackend = ClipConstructX11(&g_Ctx, false);
    if (g_Ctx.pBackend)
    {
        rc = ClipStartX11(g_Ctx.pBackend, false /* grab */);
        if (RT_SUCCESS(rc))
        {
            rc = VbglR3ClipboardConnectEx(&g_Ctx.CmdCtx);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            if (RT_SUCCESS(rc))
                rc = ShClTransferCtxInit(&g_Ctx.TransferCtx);
#endif
            if (RT_FAILURE(rc))
                ClipStopX11(g_Ctx.pBackend);
        }
    }
    else
        rc = VERR_NO_MEMORY;

    if (RT_FAILURE(rc))
    {
        VBClLogError("Error connecting to host service, rc=%Rrc\n", rc);

        VbglR3ClipboardDisconnectEx(&g_Ctx.CmdCtx);
        ClipDestructX11(g_Ctx.pBackend);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * The main loop of our clipboard reader.
 */
int vboxClipboardMain(void)
{
    LogRel(("Worker loop running\n"));

    int rc;

    SHCLCONTEXT *pCtx = &g_Ctx;

    bool fShutdown = false;

    /* The thread waits for incoming messages from the host. */
    for (;;)
    {
        PVBGLR3CLIPBOARDEVENT pEvent = NULL;

        LogFlowFunc(("Waiting for host message (fUseLegacyProtocol=%RTbool, fHostFeatures=%#RX64) ...\n",
                     pCtx->CmdCtx.fUseLegacyProtocol, pCtx->CmdCtx.fHostFeatures));

        if (pCtx->CmdCtx.fUseLegacyProtocol)
        {
            uint32_t uMsg;
            uint32_t uFormats;

            rc = VbglR3ClipboardGetHostMsgOld(pCtx->CmdCtx.uClientID, &uMsg, &uFormats);
            if (RT_FAILURE(rc))
            {
                if (rc == VERR_INTERRUPTED)
                    break;

                LogFunc(("Error getting host message, rc=%Rrc\n", rc));
            }
            else
            {
                pEvent = (PVBGLR3CLIPBOARDEVENT)RTMemAllocZ(sizeof(VBGLR3CLIPBOARDEVENT));
                AssertPtrBreakStmt(pEvent, rc = VERR_NO_MEMORY);

                switch (uMsg)
                {
                    case VBOX_SHCL_HOST_MSG_FORMATS_REPORT:
                    {
                        pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS;
                        pEvent->u.ReportedFormats.Formats = uFormats;
                        break;
                    }

                    case VBOX_SHCL_HOST_MSG_READ_DATA:
                    {
                        pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_READ_DATA;
                        pEvent->u.ReadData.uFmt = uFormats;
                        break;
                    }

                    case VBOX_SHCL_HOST_MSG_QUIT:
                    {
                        pEvent->enmType = VBGLR3CLIPBOARDEVENTTYPE_QUIT;
                        break;
                    }

                    default:
                        rc = VERR_NOT_SUPPORTED;
                        break;
                }

                if (RT_SUCCESS(rc))
                {
                    /* Copy over our command context to the event. */
                    pEvent->cmdCtx = pCtx->CmdCtx;
                }
            }
        }
        else /* Host service has peeking for messages support. */
        {
            pEvent = (PVBGLR3CLIPBOARDEVENT)RTMemAllocZ(sizeof(VBGLR3CLIPBOARDEVENT));
            AssertPtrBreakStmt(pEvent, rc = VERR_NO_MEMORY);

            uint32_t uMsg   = 0;
            uint32_t cParms = 0;
            rc = VbglR3ClipboardMsgPeekWait(&pCtx->CmdCtx, &uMsg, &cParms, NULL /* pidRestoreCheck */);
            if (RT_SUCCESS(rc))
            {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                rc = VbglR3ClipboardEventGetNextEx(uMsg, cParms, &pCtx->CmdCtx, &pCtx->TransferCtx, pEvent);
#else
                rc = VbglR3ClipboardEventGetNext(uMsg, cParms, &pCtx->CmdCtx, pEvent);
#endif
            }
        }

        if (RT_FAILURE(rc))
        {
            LogFlowFunc(("Getting next event failed with %Rrc\n", rc));

            VbglR3ClipboardEventFree(pEvent);
            pEvent = NULL;

            if (fShutdown)
                break;

            /* Wait a bit before retrying. */
            RTThreadSleep(1000);
            continue;
        }
        else
        {
            AssertPtr(pEvent);
            LogFlowFunc(("Event uType=%RU32\n", pEvent->enmType));

            switch (pEvent->enmType)
            {
                case VBGLR3CLIPBOARDEVENTTYPE_REPORT_FORMATS:
                {
                    ClipAnnounceFormatToX11(g_Ctx.pBackend, pEvent->u.ReportedFormats.Formats);
                    break;
                }

                case VBGLR3CLIPBOARDEVENTTYPE_READ_DATA:
                {
                    /* The host needs data in the specified format. */
                    CLIPREADCBREQ *pReq;
                    pReq = (CLIPREADCBREQ *)RTMemAllocZ(sizeof(CLIPREADCBREQ));
                    if (pReq)
                    {
                        pReq->Format = pEvent->u.ReadData.uFmt;
                        ClipReadDataFromX11(g_Ctx.pBackend, pReq->Format, pReq);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                    break;
                }

                case VBGLR3CLIPBOARDEVENTTYPE_QUIT:
                {
                    LogRel2(("Host requested termination\n"));
                    fShutdown = true;
                    break;
                }

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
                case VBGLR3CLIPBOARDEVENTTYPE_TRANSFER_STATUS:
                {
                    /* Nothing to do here. */
                    rc = VINF_SUCCESS;
                    break;
                }
#endif
                case VBGLR3CLIPBOARDEVENTTYPE_NONE:
                {
                    /* Nothing to do here. */
                    rc = VINF_SUCCESS;
                    break;
                }

                default:
                {
                    AssertMsgFailedBreakStmt(("Event type %RU32 not implemented\n", pEvent->enmType), rc = VERR_NOT_SUPPORTED);
                }
            }

            if (pEvent)
            {
                VbglR3ClipboardEventFree(pEvent);
                pEvent = NULL;
            }
        }

        if (fShutdown)
            break;
    }

    LogRel(("Worker loop ended\n"));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

static const char *getName()
{
    return "Shared Clipboard";
}

static const char *getPidFilePath()
{
    return ".vboxclient-clipboard.pid";
}

static int run(struct VBCLSERVICE **ppInterface, bool fDaemonised)
{
    RT_NOREF(ppInterface, fDaemonised);

    /* Initialise the guest library. */
    int rc = vboxClipboardConnect();
    if (RT_SUCCESS(rc))
    {
        rc = vboxClipboardMain();
    }

    if (RT_FAILURE(rc))
        VBClLogError("Service terminated abnormally with %Rrc\n", rc);

    if (rc == VERR_HGCM_SERVICE_NOT_FOUND)
        rc = VINF_SUCCESS; /* Prevent automatic restart by daemon script if host service not available. */

    return rc;
}

static void cleanup(struct VBCLSERVICE **ppInterface)
{
    RT_NOREF(ppInterface);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    ShClTransferCtxDestroy(&g_Ctx.TransferCtx);
#endif

    VbglR3Term();
}

struct VBCLSERVICE vbclClipboardInterface =
{
    getName,
    getPidFilePath,
    VBClServiceDefaultHandler, /* init */
    run,
    cleanup
};

struct CLIPBOARDSERVICE
{
    struct VBCLSERVICE *pInterface;
};

struct VBCLSERVICE **VBClGetClipboardService(void)
{
    struct CLIPBOARDSERVICE *pService =
        (struct CLIPBOARDSERVICE *)RTMemAlloc(sizeof(*pService));

    if (!pService)
        VBClLogFatalError("Out of memory\n");
    pService->pInterface = &vbclClipboardInterface;
    return &pService->pInterface;
}
