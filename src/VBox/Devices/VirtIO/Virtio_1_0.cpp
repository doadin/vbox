/* $Id$ */
/** @file
 * Virtio_1_0 - Virtio Common (PCI, feature & config mgt, queue mgt & proxy, notification mgt)
 */

/*
 * Copyright (C) 2009-2019 Oracle Corporation
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
#define LOG_GROUP LOG_GROUP_DEV_VIRTIO

#include <VBox/log.h>
#include <VBox/msi.h>
#include <VBox/AssertGuest.h>
#include <iprt/param.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/mem.h>
#include <iprt/assert.h>
#include <iprt/sg.h>
#include <iprt/string.h>
#include <VBox/vmm/pdmdev.h>
#include "Virtio_1_0.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define INSTANCE(a_pVirtio)                 ((a_pVirtio)->szInstance)
#define QUEUE_NAME(a_pVirtio, a_idxQueue)   ((a_pVirtio)->virtqState[(a_idxQueue)].szVirtqName)
#define IS_DRIVER_OK(a_pVirtio)             ((a_pVirtio)->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK)

/**
 * This macro returns true if the @a a_offAccess and access length (@a
 * a_cbAccess) are within the range of the mapped capability struct described by
 * @a a_LocCapData.
 *
 * @param[in]  a_offAccess      The offset into the MMIO bar of the access.
 * @param[in]  a_cbAccess       The access size.
 * @param[out] a_offIntraVar    The variable to return the intra-capability
 *                              offset into.  ASSUMES this is uint32_t.
 * @param[in]  a_LocCapData     The capability location info.
 */
#define MATCHES_VIRTIO_CAP_STRUCT(a_offAccess, a_cbAccess, a_offIntraVar, a_LocCapData) \
    (   ((a_offIntraVar) = (uint32_t)((a_offAccess) - (a_LocCapData).offMmio)) < (uint32_t)(a_LocCapData).cbMmio \
     && (a_offIntraVar) + (uint32_t)(a_cbAccess) <= (uint32_t)(a_LocCapData).cbMmio )


/** Marks the start of the virtio saved state (just for sanity). */
#define VIRTIO_SAVEDSTATE_MARKER                        UINT64_C(0x1133557799bbddff)
/** The current saved state version for the virtio core. */
#define VIRTIO_SAVEDSTATE_VERSION                       UINT32_C(1)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * virtq related structs
 * (struct names follow VirtIO 1.0 spec, typedef use VBox style)
 */
typedef struct virtq_desc
{
    uint64_t  GCPhysBuf;                                         /**< addr       GC Phys. address of buffer     */
    uint32_t  cb;                                                /**< len        Buffer length                  */
    uint16_t  fFlags;                                            /**< flags      Buffer specific flags          */
    uint16_t  uDescIdxNext;                                      /**< next       Idx set if VIRTIO_DESC_F_NEXT  */
} VIRTQ_DESC_T, *PVIRTQ_DESC_T;

typedef struct virtq_avail
{
    uint16_t  fFlags;                                            /**< flags      avail ring drv to dev flags    */
    uint16_t  uIdx;                                              /**< idx        Index of next free ring slot   */
    uint16_t  auRing[RT_FLEXIBLE_ARRAY];                         /**< ring       Ring: avail drv to dev bufs    */
    /* uint16_t  uUsedEventIdx;                                     - used_event (if VIRTQ_USED_F_EVENT_IDX)    */
} VIRTQ_AVAIL_T, *PVIRTQ_AVAIL_T;

typedef struct virtq_used_elem
{
    uint32_t  uDescIdx;                                          /**< idx         Start of used desc chain      */
    uint32_t  cbElem;                                            /**< len         Total len of used desc chain  */
} VIRTQ_USED_ELEM_T;

typedef struct virt_used
{
    uint16_t  fFlags;                                            /**< flags       used ring host-to-guest flags */
    uint16_t  uIdx;                                              /**< idx         Index of next ring slot       */
    VIRTQ_USED_ELEM_T aRing[RT_FLEXIBLE_ARRAY];                  /**< ring        Ring: used dev to drv bufs    */
    /* uint16_t  uAvailEventIdx;                                    - avail_event if (VIRTQ_USED_F_EVENT_IDX)   */
} VIRTQ_USED_T, *PVIRTQ_USED_T;


const char *virtioCoreGetStateChangeText(VIRTIOVMSTATECHANGED enmState)
{
    switch (enmState)
    {
        case kvirtIoVmStateChangedReset:                return "VM RESET";
        case kvirtIoVmStateChangedSuspend:              return "VM SUSPEND";
        case kvirtIoVmStateChangedPowerOff:             return "VM POWER OFF";
        case kvirtIoVmStateChangedResume:               return "VM RESUME";
        default:                                        return "<BAD ENUM>";
    }
}

/* Internal Functions */

static void virtioNotifyGuestDriver(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue, bool fForce);
static int  virtioKick(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint8_t uCause, uint16_t uVec, bool fForce);

/** @name Internal queue operations
 * @{ */

#if 0 /* unused */
DECLINLINE(int) virtqIsEventNeeded(uint16_t uEventIdx, uint16_t uDescIdxNew, uint16_t uDescIdxOld)
{
    return (uint16_t)(uDescIdxNew - uEventIdx - 1) < (uint16_t)(uDescIdxNew - uDescIdxOld);
}
#endif

/**
 * Accessor for virtq descriptor
 */
DECLINLINE(void) virtioReadDesc(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue,
                                uint32_t idxDesc, PVIRTQ_DESC_T pDesc)
{
    //Log(("%s virtioQueueReadDesc: ring=%p idx=%u\n", INSTANCE(pState), pVirtQ, idx));
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    uint16_t const cQueueItems = RT_MAX(pVirtio->uQueueSize[idxQueue], 1); /* Make sure to avoid div-by-zero. */
    PDMDevHlpPCIPhysRead(pDevIns,
                      pVirtio->aGCPhysQueueDesc[idxQueue] + sizeof(VIRTQ_DESC_T) * (idxDesc % cQueueItems),
                      pDesc, sizeof(VIRTQ_DESC_T));
}

/**
 * Accessors for virtq avail ring
 */
DECLINLINE(uint16_t) virtioReadAvailDescIdx(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue, uint32_t availIdx)
{
    uint16_t uDescIdx;
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    uint16_t const cQueueItems = RT_MAX(pVirtio->uQueueSize[idxQueue], 1); /* Make sure to avoid div-by-zero. */
    PDMDevHlpPCIPhysRead(pDevIns,
                        pVirtio->aGCPhysQueueAvail[idxQueue]
                      + RT_UOFFSETOF_DYN(VIRTQ_AVAIL_T, auRing[availIdx % cQueueItems]),
                      &uDescIdx, sizeof(uDescIdx));
    return uDescIdx;
}

DECLINLINE(uint16_t) virtioReadAvailRingIdx(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue)
{
    uint16_t uIdx = 0;
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    PDMDevHlpPCIPhysRead(pDevIns,
                      pVirtio->aGCPhysQueueAvail[idxQueue] + RT_UOFFSETOF(VIRTQ_AVAIL_T, uIdx),
                      &uIdx, sizeof(uIdx));
    return uIdx;
}

DECLINLINE(bool) virtqIsEmpty(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue)
{
    return virtioReadAvailRingIdx(pDevIns, pVirtio, idxQueue) == pVirtio->virtqState[idxQueue].uAvailIdx;
}

#if 0 /* unused - Will be used when VIRTIO_F_EVENT_IDX optional feature is implemented, VirtIO 1.0, 2.4.7 */
DECLINLINE(uint16_t) virtioReadAvailFlags(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue)
{
    uint16_t fFlags;
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    PDMDevHlpPCIPhysRead(pDevIns,
                      pVirtio->aGCPhysQueueAvail[idxQueue] + RT_UOFFSETOF(VIRTQ_AVAIL_T, fFlags),
                      &fFlags, sizeof(fFlags));
    return fFlags;
}
#endif

DECLINLINE(uint16_t) virtioReadAvailUsedEvent(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue)
{
    uint16_t uUsedEventIdx;
    /* VirtIO 1.0 uUsedEventIdx (used_event) immediately follows ring */
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    PDMDevHlpPCIPhysRead(pDevIns,
                      pVirtio->aGCPhysQueueAvail[idxQueue] + RT_UOFFSETOF_DYN(VIRTQ_AVAIL_T, auRing[pVirtio->uQueueSize[idxQueue]]),
                      &uUsedEventIdx, sizeof(uUsedEventIdx));
    return uUsedEventIdx;
}
/** @} */

/** @name Accessors for virtq used ring
 * @{
 */
DECLINLINE(void) virtioWriteUsedElem(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue,
                                     uint32_t usedIdx, uint32_t uDescIdx, uint32_t uLen)
{
    VIRTQ_USED_ELEM_T elem = { uDescIdx,  uLen };
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    uint16_t const cQueueItems = RT_MAX(pVirtio->uQueueSize[idxQueue], 1); /* Make sure to avoid div-by-zero. */
    PDMDevHlpPCIPhysWrite(pDevIns,
                          pVirtio->aGCPhysQueueUsed[idxQueue] + RT_UOFFSETOF_DYN(VIRTQ_USED_T, aRing[usedIdx % cQueueItems]),
                          &elem, sizeof(elem));
}

DECLINLINE(void) virtioWriteUsedRingIdx(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue, uint16_t uIdx)
{
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    PDMDevHlpPCIPhysWrite(pDevIns,
                          pVirtio->aGCPhysQueueUsed[idxQueue] + RT_UOFFSETOF(VIRTQ_USED_T, uIdx),
                          &uIdx, sizeof(uIdx));
}

#ifdef LOG_ENABLED
DECLINLINE(uint16_t) virtioReadUsedRingIdx(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue)
{
    uint16_t uIdx = 0;
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    PDMDevHlpPCIPhysRead(pDevIns,
                      pVirtio->aGCPhysQueueUsed[idxQueue] + RT_UOFFSETOF(VIRTQ_USED_T, uIdx),
                      &uIdx, sizeof(uIdx));
    return uIdx;
}
#endif

DECLINLINE(uint16_t) virtioReadUsedFlags(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue)
{
    uint16_t fFlags = 0;
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    PDMDevHlpPCIPhysRead(pDevIns,
                      pVirtio->aGCPhysQueueUsed[idxQueue] + RT_UOFFSETOF(VIRTQ_USED_T, fFlags),
                      &fFlags, sizeof(fFlags));
    return fFlags;
}

#if 0 /* unused - This may eventually be used to set no-notify for the ring as an optimization */
DECLINLINE(void) virtioWriteUsedFlags(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue, uint32_t fFlags)
{
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    RT_UNTRUSTED_VALIDATED_FENCE(); /* VirtIO 1.0, Section 3.2.1.4.1 */
    PDMDevHlpPCIPhysWrite(pDevIns,
                          pVirtio->aGCPhysQueueUsed[idxQueue] + RT_UOFFSETOF(VIRTQ_USED_T, fFlags),
                          &fFlags, sizeof(fFlags));
}
#endif

#if 0 /* unused - *May* be used when VIRTIO_F_EVENT_IDX optional feature is implemented VirtIO 1.0, 2.4.9.2*/
DECLINLINE(void) virtioWriteUsedAvailEvent(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue, uint32_t uAvailEventIdx)
{
    /** VirtIO 1.0 uAvailEventIdx (avail_event) immediately follows ring */
    AssertMsg(pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK, ("Called with guest driver not ready\n"));
    PDMDevHlpPCIPhysWrite(pDevIns,
                          pVirtio->aGCPhysQueueUsed[idxQueue] + RT_UOFFSETOF_DYN(VIRTQ_USED_T, aRing[pVirtio->uQueueSize[idxQueue]]),
                          &uAvailEventIdx, sizeof(uAvailEventIdx));
}
#endif

/** @} */

void virtioCoreSgBufInit(PVIRTIOSGBUF pGcSgBuf, PVIRTIOSGSEG paSegs, size_t cSegs)
{
    AssertPtr(pGcSgBuf);
    Assert(   (cSegs > 0 && VALID_PTR(paSegs)) || (!cSegs && !paSegs));
    Assert(cSegs < (~(unsigned)0 >> 1));

    pGcSgBuf->paSegs = paSegs;
    pGcSgBuf->cSegs  = (unsigned)cSegs;
    pGcSgBuf->idxSeg = 0;
    if (cSegs && paSegs)
    {
        pGcSgBuf->pGcSegCur = paSegs[0].pGcSeg;
        pGcSgBuf->cbSegLeft = paSegs[0].cbSeg;
    }
    else
    {
        pGcSgBuf->pGcSegCur = 0;
        pGcSgBuf->cbSegLeft = 0;
    }
}

static RTGCPHYS virtioCoreSgBufGet(PVIRTIOSGBUF pGcSgBuf, size_t *pcbData)
{
    size_t cbData;
    RTGCPHYS pGcBuf;

    /* Check that the S/G buffer has memory left. */
    if (RT_LIKELY(pGcSgBuf->idxSeg < pGcSgBuf->cSegs && pGcSgBuf->cbSegLeft))
    { /* likely */ }
    else
    {
        *pcbData = 0;
        return 0;
    }

    AssertMsg(    pGcSgBuf->cbSegLeft <= 128 * _1M
              && (RTGCPHYS)pGcSgBuf->pGcSegCur >= (RTGCPHYS)pGcSgBuf->paSegs[pGcSgBuf->idxSeg].pGcSeg
              && (RTGCPHYS)pGcSgBuf->pGcSegCur + pGcSgBuf->cbSegLeft <=
                   (RTGCPHYS)pGcSgBuf->paSegs[pGcSgBuf->idxSeg].pGcSeg + pGcSgBuf->paSegs[pGcSgBuf->idxSeg].cbSeg,
                 ("pGcSgBuf->idxSeg=%d pGcSgBuf->cSegs=%d pGcSgBuf->pGcSegCur=%p pGcSgBuf->cbSegLeft=%zd "
                  "pGcSgBuf->paSegs[%d].pGcSeg=%p pGcSgBuf->paSegs[%d].cbSeg=%zd\n",
                  pGcSgBuf->idxSeg, pGcSgBuf->cSegs, pGcSgBuf->pGcSegCur, pGcSgBuf->cbSegLeft,
                  pGcSgBuf->idxSeg, pGcSgBuf->paSegs[pGcSgBuf->idxSeg].pGcSeg, pGcSgBuf->idxSeg,
                  pGcSgBuf->paSegs[pGcSgBuf->idxSeg].cbSeg));

    cbData = RT_MIN(*pcbData, pGcSgBuf->cbSegLeft);
    pGcBuf = pGcSgBuf->pGcSegCur;
    pGcSgBuf->cbSegLeft -= cbData;
    if (!pGcSgBuf->cbSegLeft)
    {
        pGcSgBuf->idxSeg++;

        if (pGcSgBuf->idxSeg < pGcSgBuf->cSegs)
        {
            pGcSgBuf->pGcSegCur = pGcSgBuf->paSegs[pGcSgBuf->idxSeg].pGcSeg;
            pGcSgBuf->cbSegLeft = pGcSgBuf->paSegs[pGcSgBuf->idxSeg].cbSeg;
        }
        *pcbData = cbData;
    }
    else
        pGcSgBuf->pGcSegCur = pGcSgBuf->pGcSegCur + cbData;

    return pGcBuf;
}

void virtioCoreSgBufReset(PVIRTIOSGBUF pGcSgBuf)
{
    AssertPtrReturnVoid(pGcSgBuf);

    pGcSgBuf->idxSeg = 0;
    if (pGcSgBuf->cSegs)
    {
        pGcSgBuf->pGcSegCur = pGcSgBuf->paSegs[0].pGcSeg;
        pGcSgBuf->cbSegLeft = pGcSgBuf->paSegs[0].cbSeg;
    }
    else
    {
        pGcSgBuf->pGcSegCur = 0;
        pGcSgBuf->cbSegLeft = 0;
    }
}

RTGCPHYS virtioCoreSgBufAdvance(PVIRTIOSGBUF pGcSgBuf, size_t cbAdvance)
{
    AssertReturn(pGcSgBuf, 0);

    size_t cbLeft = cbAdvance;
    while (cbLeft)
    {
        size_t cbThisAdvance = cbLeft;
        virtioCoreSgBufGet(pGcSgBuf, &cbThisAdvance);
        if (!cbThisAdvance)
            break;

        cbLeft -= cbThisAdvance;
    }
    return cbAdvance - cbLeft;
}

RTGCPHYS virtioCoreSgBufGetNextSegment(PVIRTIOSGBUF pGcSgBuf, size_t *pcbSeg)
{
    AssertReturn(pGcSgBuf, 0);
    AssertPtrReturn(pcbSeg, 0);

    if (!*pcbSeg)
        *pcbSeg = pGcSgBuf->cbSegLeft;

    return virtioCoreSgBufGet(pGcSgBuf, pcbSeg);
}

#ifdef LOG_ENABLED

/**
 * Does a formatted hex dump using Log(()), recommend using VIRTIO_HEX_DUMP() macro to
 * control enabling of logging efficiently.
 *
 * @param   pv          pointer to buffer to dump contents of
 * @param   cb          count of characters to dump from buffer
 * @param   uBase       base address of per-row address prefixing of hex output
 * @param   pszTitle    Optional title. If present displays title that lists
 *                      provided text with value of cb to indicate size next to it.
 */
void virtioCoreHexDump(uint8_t *pv, uint32_t cb, uint32_t uBase, const char *pszTitle)
{
    if (pszTitle)
        Log(("%s [%d bytes]:\n", pszTitle, cb));
    for (uint32_t row = 0; row < RT_MAX(1, (cb / 16) + 1) && row * 16 < cb; row++)
    {
        Log(("%04x: ", row * 16 + uBase)); /* line address */
        for (uint8_t col = 0; col < 16; col++)
        {
           uint32_t idx = row * 16 + col;
           if (idx >= cb)
               Log(("-- %s", (col + 1) % 8 ? "" : "  "));
           else
               Log(("%02x %s", pv[idx], (col + 1) % 8 ? "" : "  "));
        }
        for (uint32_t idx = row * 16; idx < row * 16 + 16; idx++)
           Log(("%c", (idx >= cb) ? ' ' : (pv[idx] >= 0x20 && pv[idx] <= 0x7e ? pv[idx] : '.')));
        Log(("\n"));
    }
    Log(("\n"));
    RT_NOREF2(uBase, pv);
}

#endif /* LOG_ENABLED */

/**
 * Log memory-mapped I/O input or output value.
 *
 * This is designed to be invoked by macros that can make contextual assumptions
 * (e.g. implicitly derive MACRO parameters from the invoking function). It is exposed
 * for the VirtIO client doing the device-specific implementation in order to log in a
 * similar fashion accesses to the device-specific MMIO configuration structure. Macros
 * that leverage this function are found in virtioCommonCfgAccessed() and can be
 * used as an example of how to use this effectively for the device-specific
 * code.
 *
 * @param   pszFunc     To avoid displaying this function's name via __FUNCTION__ or LogFunc()
 * @param   pszMember   Name of struct member
 * @param   pv          pointer to value
 * @param   cb          size of value
 * @param   uOffset     offset into member where value starts
 * @param   fWrite      True if write I/O
 * @param   fHasIndex   True if the member is indexed
 * @param   idx         The index if fHasIndex
 */
void virtioCoreLogMappedIoValue(const char *pszFunc, const char *pszMember, uint32_t uMemberSize,
                                const void *pv, uint32_t cb, uint32_t uOffset, int fWrite,
                                int fHasIndex, uint32_t idx)
{
    if (!LogIs6Enabled())
        return;

    char szIdx[16];
    if (fHasIndex)
        RTStrPrintf(szIdx, sizeof(szIdx), "[%d]", idx);
    else
        szIdx[0] = '\0';

    if (cb == 1 || cb == 2 || cb == 4 || cb == 8)
    {
        char szDepiction[64];
        size_t cchDepiction;
        if (uOffset != 0 || cb != uMemberSize) /* display bounds if partial member access */
            cchDepiction = RTStrPrintf(szDepiction, sizeof(szDepiction), "%s%s[%d:%d]",
                                       pszMember, szIdx, uOffset, uOffset + cb - 1);
        else
            cchDepiction = RTStrPrintf(szDepiction, sizeof(szDepiction), "%s%s", pszMember, szIdx);

        /* padding */
        if (cchDepiction < 30)
            szDepiction[cchDepiction++] = ' ';
        while (cchDepiction < 30)
            szDepiction[cchDepiction++] = '.';
        szDepiction[cchDepiction] = '\0';

        RTUINT64U uValue;
        uValue.u = 0;
        memcpy(uValue.au8, pv, cb);
        Log6(("%s: Guest %s %s %#0*RX64\n",
                  pszFunc, fWrite ? "wrote" : "read ", szDepiction, 2 + cb * 2, uValue.u));
    }
    else /* odd number or oversized access, ... log inline hex-dump style */
    {
        Log6(("%s: Guest %s %s%s[%d:%d]: %.*Rhxs\n",
                  pszFunc, fWrite ? "wrote" : "read ", pszMember,
                  szIdx, uOffset, uOffset + cb, cb, pv));
    }
    RT_NOREF2(fWrite, pszFunc);
}


/**
 * Makes the MMIO-mapped Virtio uDeviceStatus registers non-cryptic
 */
DECLINLINE(void) virtioLogDeviceStatus(uint8_t bStatus)
{
    if (bStatus == 0)
        Log6(("RESET"));
    else
    {
        int primed = 0;
        if (bStatus & VIRTIO_STATUS_ACKNOWLEDGE)
            Log6(("%sACKNOWLEDGE", primed++ ? "" : ""));
        if (bStatus & VIRTIO_STATUS_DRIVER)
            Log6(("%sDRIVER",      primed++ ? " | " : ""));
        if (bStatus & VIRTIO_STATUS_FEATURES_OK)
            Log6(("%sFEATURES_OK", primed++ ? " | " : ""));
        if (bStatus & VIRTIO_STATUS_DRIVER_OK)
            Log6(("%sDRIVER_OK",   primed++ ? " | " : ""));
        if (bStatus & VIRTIO_STATUS_FAILED)
            Log6(("%sFAILED",      primed++ ? " | " : ""));
        if (bStatus & VIRTIO_STATUS_DEVICE_NEEDS_RESET)
            Log6(("%sNEEDS_RESET", primed++ ? " | " : ""));
        (void)primed;
    }
}

#ifdef IN_RING3
/**
 * Allocate client context for client to work with VirtIO-provided with queue
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   idxQueue    Queue number
 * @param   pcszName    Name to give queue
 *
 * @returns VBox status code.
 */
int virtioCoreR3QueueAttach(PVIRTIOCORE pVirtio, uint16_t idxQueue, const char *pcszName)
{
    LogFunc(("%s\n", pcszName));
    PVIRTQSTATE pVirtq = &pVirtio->virtqState[idxQueue];
    pVirtq->uAvailIdx = 0;
    pVirtq->uUsedIdx  = 0;
    pVirtq->fEventThresholdReached = false;
    RTStrCopy(pVirtq->szVirtqName, sizeof(pVirtq->szVirtqName), pcszName);
    return VINF_SUCCESS;
}
#endif /* IN_RING3 */

/**
 * See API comments in header file for description
 */
int virtioQueueSkip(PVIRTIOCORE pVirtio, uint16_t idxQueue)
{
    Assert(idxQueue < RT_ELEMENTS(pVirtio->virtqState));
    PVIRTQSTATE pVirtq = &pVirtio->virtqState[idxQueue];

    AssertMsgReturn(IS_DRIVER_OK(pVirtio) && pVirtio->uQueueEnable[idxQueue],
                    ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    if (virtioCoreQueueIsEmpty(pVirtio->pDevIns, pVirtio, idxQueue))
        return VERR_NOT_AVAILABLE;

    Log2Func(("%s avail_idx=%u\n", pVirtq->szVirtqName, pVirtq->uAvailIdx));
    pVirtq->uAvailIdx++;

    return VINF_SUCCESS;
}

/**
 * Check if the associated queue is empty
 *
 * @param   pDevIns     The device instance (for reading).
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   idxQueue    Queue number
 *
 * @retval true  Queue is empty or unavailable.
 * @retval false Queue is available and has entries
 */
bool virtioCoreQueueIsEmpty(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue)
{
    if (pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK)
        return virtqIsEmpty(pDevIns, pVirtio, idxQueue);
    return true;
}

#ifdef IN_RING3


int virtioCoreR3DescChainGet(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue,
                             uint16_t uHeadIdx, PPVIRTIO_DESC_CHAIN_T ppDescChain)
{
    AssertReturn(ppDescChain, VERR_INVALID_PARAMETER);

    Assert(idxQueue < RT_ELEMENTS(pVirtio->virtqState));

    PVIRTQSTATE pVirtq  = &pVirtio->virtqState[idxQueue];

    PVIRTIOSGSEG paSegsIn = (PVIRTIOSGSEG)RTMemAlloc(VIRTQ_MAX_SIZE * sizeof(VIRTIOSGSEG));
    AssertReturn(paSegsIn, VERR_NO_MEMORY);

    PVIRTIOSGSEG paSegsOut = (PVIRTIOSGSEG)RTMemAlloc(VIRTQ_MAX_SIZE * sizeof(VIRTIOSGSEG));
    AssertReturn(paSegsOut, VERR_NO_MEMORY);

    AssertMsgReturn(IS_DRIVER_OK(pVirtio) && pVirtio->uQueueEnable[idxQueue],
                    ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    uint16_t uDescIdx = uHeadIdx;

    Log3Func(("%s DESC CHAIN: (head) desc_idx=%u\n", pVirtq->szVirtqName, uHeadIdx));
    RT_NOREF(pVirtq);

    VIRTQ_DESC_T desc;

    uint32_t cbIn = 0, cbOut = 0, cSegsIn = 0, cSegsOut = 0;

    do
    {
        PVIRTIOSGSEG pSeg;

        /*
         * Malicious guests may go beyond paSegsIn or paSegsOut boundaries by linking
         * several descriptors into a loop. Since there is no legitimate way to get a sequences of
         * linked descriptors exceeding the total number of descriptors in the ring (see @bugref{8620}),
         * the following aborts I/O if breach and employs a simple log throttling algorithm to notify.
         */
        if (cSegsIn + cSegsOut >= VIRTQ_MAX_SIZE)
        {
            static volatile uint32_t s_cMessages  = 0;
            static volatile uint32_t s_cThreshold = 1;
            if (ASMAtomicIncU32(&s_cMessages) == ASMAtomicReadU32(&s_cThreshold))
            {
                LogRelMax(64, ("Too many linked descriptors; check if the guest arranges descriptors in a loop.\n"));
                if (ASMAtomicReadU32(&s_cMessages) != 1)
                    LogRelMax(64, ("(the above error has occured %u times so far)\n", ASMAtomicReadU32(&s_cMessages)));
                ASMAtomicWriteU32(&s_cThreshold, ASMAtomicReadU32(&s_cThreshold) * 10);
            }
            break;
        }
        RT_UNTRUSTED_VALIDATED_FENCE();

        virtioReadDesc(pDevIns, pVirtio, idxQueue, uDescIdx, &desc);

        if (desc.fFlags & VIRTQ_DESC_F_WRITE)
        {
            Log3Func(("%s IN  desc_idx=%u seg=%u addr=%RGp cb=%u\n", QUEUE_NAME(pVirtio, idxQueue), uDescIdx, cSegsIn, desc.GCPhysBuf, desc.cb));
            cbIn += desc.cb;
            pSeg = &(paSegsIn[cSegsIn++]);
        }
        else
        {
            Log3Func(("%s OUT desc_idx=%u seg=%u addr=%RGp cb=%u\n", QUEUE_NAME(pVirtio, idxQueue), uDescIdx, cSegsOut, desc.GCPhysBuf, desc.cb));
            cbOut += desc.cb;
            pSeg = &(paSegsOut[cSegsOut++]);
        }

        pSeg->pGcSeg = desc.GCPhysBuf;
        pSeg->cbSeg = desc.cb;

        uDescIdx = desc.uDescIdxNext;
    } while (desc.fFlags & VIRTQ_DESC_F_NEXT);

    PVIRTIOSGBUF pSgPhysIn = (PVIRTIOSGBUF)RTMemAllocZ(sizeof(VIRTIOSGBUF));
    AssertReturn(pSgPhysIn, VERR_NO_MEMORY);

    virtioCoreSgBufInit(pSgPhysIn, paSegsIn, cSegsIn);

    PVIRTIOSGBUF pSgPhysOut = (PVIRTIOSGBUF)RTMemAllocZ(sizeof(VIRTIOSGBUF));
    AssertReturn(pSgPhysOut, VERR_NO_MEMORY);

    virtioCoreSgBufInit(pSgPhysOut, paSegsOut, cSegsOut);

    PVIRTIO_DESC_CHAIN_T pDescChain = (PVIRTIO_DESC_CHAIN_T)RTMemAllocZ(sizeof(VIRTIO_DESC_CHAIN_T));
    AssertReturn(pDescChain, VERR_NO_MEMORY);

    pDescChain->uHeadIdx      = uHeadIdx;
    pDescChain->cbPhysSend    = cbOut;
    pDescChain->pSgPhysSend   = pSgPhysOut;
    pDescChain->cbPhysReturn  = cbIn;
    pDescChain->pSgPhysReturn = pSgPhysIn;
    *ppDescChain = pDescChain;

    Log3Func(("%s -- segs OUT: %u (%u bytes)   IN: %u (%u bytes) --\n", pVirtq->szVirtqName, cSegsOut, cbOut, cSegsIn, cbIn));

    return VINF_SUCCESS;
}

/**
 * Fetches descriptor chain using avail ring of indicated queue and converts the descriptor
 * chain into its OUT (to device) and IN to guest components.
 *
 * Additionally it converts the OUT desc chain data to a contiguous virtual
 * memory buffer for easy consumption by the caller. The caller must return the
 * descriptor chain pointer via virtioCoreR3QueuePut() and then call virtioCoreQueueSync()
 * at some point to return the data to the guest and complete the transaction.
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   idxQueue    Queue number
 * @param   fRemove     flags whether to remove desc chain from queue (false = peek)
 * @param   ppDescChain Address to store pointer to descriptor chain that contains the
 *                      pre-processed transaction information pulled from the virtq.
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS         Success
 * @retval  VERR_INVALID_STATE   VirtIO not in ready state (asserted).
 * @retval  VERR_NOT_AVAILABLE   If the queue is empty.
 */
int virtioCoreR3QueueGet(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue,
                         PPVIRTIO_DESC_CHAIN_T ppDescChain, bool fRemove)
{
    PVIRTQSTATE pVirtq = &pVirtio->virtqState[idxQueue];

    if (virtqIsEmpty(pDevIns, pVirtio, idxQueue))
        return VERR_NOT_AVAILABLE;

    uint16_t uHeadIdx = virtioReadAvailDescIdx(pDevIns, pVirtio, idxQueue, pVirtq->uAvailIdx);

    if (fRemove)
        pVirtq->uAvailIdx++;

    int rc = virtioCoreR3DescChainGet(pDevIns, pVirtio, idxQueue, uHeadIdx, ppDescChain);
    return rc;
}

/**
 * Returns data to the guest to complete a transaction initiated by virtQueueGet().
 *
 * The caller passes in a pointer to a scatter-gather buffer of virtual memory segments
 * and a pointer to the descriptor chain context originally derived from the pulled
 * queue entry, and this function will write the virtual memory s/g buffer into the
 * guest's physical memory free the descriptor chain. The caller handles the freeing
 * (as needed) of the virtual memory buffer.
 *
 * @note This does a write-ahead to the used ring of the guest's queue. The data
 *       written won't be seen by the guest until the next call to virtioCoreQueueSync()
 *
 *
 * @param   pDevIns         The device instance (for reading).
 * @param   pVirtio         Pointer to the shared virtio state.
 * @param   idxQueue        Queue number
 *
 * @param   pSgVirtReturn   Points toscatter-gather buffer of virtual memory
 *                          segments the caller is returning to the guest.
 *
 * @param   pDescChain      This contains the context of the scatter-gather
 *                          buffer originally pulled from the queue.
 *
 * @param   fFence          If true, put up copy fence (memory barrier) after
 *                          copying to guest phys. mem.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS       Success
 * @retval  VERR_INVALID_STATE VirtIO not in ready state
 * @retval  VERR_NOT_AVAILABLE Queue is empty
 */
int virtioCoreR3QueuePut(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue, PRTSGBUF pSgVirtReturn,
                         PVIRTIO_DESC_CHAIN_T pDescChain, bool fFence)
{
    Assert(idxQueue < RT_ELEMENTS(pVirtio->virtqState));
    PVIRTQSTATE pVirtq = &pVirtio->virtqState[idxQueue];
    PVIRTIOSGBUF pSgPhysReturn = pDescChain->pSgPhysReturn;

    AssertMsgReturn(IS_DRIVER_OK(pVirtio) /*&& pVirtio->uQueueEnable[idxQueue]*/,
                    ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    Log3Func(("Copying client data to %s, desc chain (head desc_idx %d)\n",
              QUEUE_NAME(pVirtio, idxQueue), virtioReadUsedRingIdx(pDevIns, pVirtio, idxQueue)));

    /*
     * Copy s/g buf (virtual memory) to guest phys mem (IN direction). This virtual memory
     * block will be small (fixed portion of response header + sense buffer area or
     * control commands or error return values)... The bulk of req data xfers to phys mem
     * is handled by client */

    size_t cbCopy = 0;
    size_t cbRemain = RTSgBufCalcTotalLength(pSgVirtReturn);
    virtioCoreSgBufReset(pSgPhysReturn); /* Reset ptr because req data may have already been written */
    while (cbRemain)
    {
        PVIRTIOSGSEG paSeg = &pSgPhysReturn->paSegs[pSgPhysReturn->idxSeg];
        uint64_t dstSgStart = (uint64_t)paSeg->pGcSeg;
        uint64_t dstSgLen   = (uint64_t)paSeg->cbSeg;
        uint64_t dstSgCur   = (uint64_t)pSgPhysReturn->pGcSegCur;
        cbCopy = RT_MIN((uint64_t)pSgVirtReturn->cbSegLeft, dstSgLen - (dstSgCur - dstSgStart));
        PDMDevHlpPhysWrite(pDevIns, (RTGCPHYS)pSgPhysReturn->pGcSegCur, pSgVirtReturn->pvSegCur, cbCopy);
        RTSgBufAdvance(pSgVirtReturn, cbCopy);
        virtioCoreSgBufAdvance(pSgPhysReturn, cbCopy);
        cbRemain -= cbCopy;
    }

    if (fFence)
        RT_UNTRUSTED_NONVOLATILE_COPY_FENCE(); /* needed? */

    /* If this write-ahead crosses threshold where the driver wants to get an event flag it */
    if (pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX)
        if (pVirtq->uUsedIdx == virtioReadAvailUsedEvent(pDevIns, pVirtio, idxQueue))
            pVirtq->fEventThresholdReached = true;

    Assert(!(cbCopy >> 32));

    /*
     * Place used buffer's descriptor in used ring but don't update used ring's slot index.
     * That will be done with a subsequent client call to virtioCoreQueueSync() */
    virtioWriteUsedElem(pDevIns, pVirtio, idxQueue, pVirtq->uUsedIdx++, pDescChain->uHeadIdx, (uint32_t)cbCopy);

    Log2Func((".... Copied %zu bytes to %u byte buffer, residual=%zu\n",
              cbCopy, pDescChain->cbPhysReturn, pDescChain->cbPhysReturn - cbCopy));

    Log6Func(("Write ahead used_idx=%u, %s used_idx=%u\n",
              pVirtq->uUsedIdx, QUEUE_NAME(pVirtio, idxQueue), virtioReadUsedRingIdx(pDevIns, pVirtio, idxQueue)));

    RTMemFree((void *)pDescChain->pSgPhysSend->paSegs);
    RTMemFree(pDescChain->pSgPhysSend);
    RTMemFree((void *)pSgPhysReturn->paSegs);
    RTMemFree(pSgPhysReturn);
    RTMemFree(pDescChain);

    return VINF_SUCCESS;
}

#endif /* IN_RING3 */

/**
 * Updates the indicated virtq's "used ring" descriptor index to match the
 * current write-head index, thus exposing the data added to the used ring by all
 * virtioCoreR3QueuePut() calls since the last sync. This should be called after one or
 * more virtioCoreR3QueuePut() calls to inform the guest driver there is data in the queue.
 * Explicit notifications (e.g. interrupt or MSI-X) will be sent to the guest,
 * depending on VirtIO features negotiated and conditions, otherwise the guest
 * will detect the update by polling. (see VirtIO 1.0
 * specification, Section 2.4 "Virtqueues").
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   idxQueue    Queue number
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS       Success
 * @retval  VERR_INVALID_STATE VirtIO not in ready state
 */
int virtioCoreQueueSync(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue)
{
    Assert(idxQueue < RT_ELEMENTS(pVirtio->virtqState));
    PVIRTQSTATE pVirtq = &pVirtio->virtqState[idxQueue];

    AssertMsgReturn(IS_DRIVER_OK(pVirtio) && pVirtio->uQueueEnable[idxQueue],
                    ("Guest driver not in ready state.\n"), VERR_INVALID_STATE);

    Log6Func(("Updating %s used_idx from %u to %u\n",
              QUEUE_NAME(pVirtio, idxQueue), virtioReadUsedRingIdx(pDevIns, pVirtio, idxQueue), pVirtq->uUsedIdx));

    virtioWriteUsedRingIdx(pDevIns, pVirtio, idxQueue, pVirtq->uUsedIdx);
    virtioNotifyGuestDriver(pDevIns, pVirtio, idxQueue, false);

    return VINF_SUCCESS;
}

#ifdef IN_RING3
/**
 */
static void virtioR3QueueNotified(PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC, uint16_t idxQueue, uint16_t uNotifyIdx)
{
    /* See VirtIO 1.0, section 4.1.5.2 It implies that idxQueue and uNotifyIdx should match.
     * Disregarding this notification may cause throughput to stop, however there's no way to know
     * which was queue was intended for wake-up if the two parameters disagree. */

    AssertMsg(uNotifyIdx == idxQueue,
              ("Notification param disagreement. Guest kicked virtq %d's notify addr w/non-corresponding virtq idx %d\n",
               idxQueue, uNotifyIdx));

//    AssertMsgReturn(uNotifyIdx == idxQueue,
//                    ("Notification param disagreement. Guest kicked virtq %d's notify addr w/non-corresponding virtq idx %d\n",
//                     idxQueue, uNotifyIdx));
    RT_NOREF(uNotifyIdx);

    AssertReturnVoid(idxQueue < RT_ELEMENTS(pVirtio->virtqState));
    Log6Func(("%s\n", pVirtio->virtqState[idxQueue].szVirtqName));

    /* Inform client */
    pVirtioCC->pfnQueueNotified(pVirtio, pVirtioCC, idxQueue);
}
#endif /* IN_RING3 */

/**
 * Trigger MSI-X or INT# interrupt to notify guest of data added to used ring of
 * the specified virtq, depending on the interrupt configuration of the device
 * and depending on negotiated and realtime constraints flagged by the guest driver.
 *
 * See VirtIO 1.0 specification (section 2.4.7).
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   idxQueue    Queue to check for guest interrupt handling preference
 * @param   fForce      Overrides idxQueue, forcing notification regardless of driver's
 *                      notification preferences. This is a safeguard to prevent
 *                      stalls upon resuming the VM. VirtIO 1.0 specification Section 4.1.5.5
 *                      indicates spurious interrupts are harmless to guest driver's state,
 *                      as they only cause the guest driver to [re]scan queues for work to do.
 */
static void virtioNotifyGuestDriver(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t idxQueue, bool fForce)
{

    Assert(idxQueue < RT_ELEMENTS(pVirtio->virtqState));
    PVIRTQSTATE pVirtq = &pVirtio->virtqState[idxQueue];

    AssertMsgReturnVoid(IS_DRIVER_OK(pVirtio), ("Guest driver not in ready state.\n"));
    if (pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX)
    {
        if (pVirtq->fEventThresholdReached)
        {
            virtioKick(pDevIns, pVirtio, VIRTIO_ISR_VIRTQ_INTERRUPT, pVirtio->uQueueMsixVector[idxQueue], fForce);
            pVirtq->fEventThresholdReached = false;
            return;
        }
        Log6Func(("...skipping interrupt: VIRTIO_F_EVENT_IDX set but threshold not reached\n"));
    }
    else
    {
        /** If guest driver hasn't suppressed interrupts, interrupt  */
        if (fForce || !(virtioReadUsedFlags(pDevIns, pVirtio, idxQueue) & VIRTQ_AVAIL_F_NO_INTERRUPT))
        {
            virtioKick(pDevIns, pVirtio, VIRTIO_ISR_VIRTQ_INTERRUPT, pVirtio->uQueueMsixVector[idxQueue], fForce);
            return;
        }
        Log6Func(("...skipping interrupt. Guest flagged VIRTQ_AVAIL_F_NO_INTERRUPT for queue\n"));
    }
}

/**
 * Raise interrupt or MSI-X
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uCause      Interrupt cause bit mask to set in PCI ISR port.
 * @param   uVec        MSI-X vector, if enabled
 * @param   uForce      True of out-of-band
 */
static int virtioKick(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint8_t uCause, uint16_t uMsixVector, bool fForce)
{
    if (fForce)
        Log6Func(("reason: resumed after suspend\n"));
    else
    if (uCause == VIRTIO_ISR_VIRTQ_INTERRUPT)
        Log6Func(("reason: buffer added to 'used' ring.\n"));
    else
    if (uCause == VIRTIO_ISR_DEVICE_CONFIG)
       Log6Func(("reason: device config change\n"));

    if (!pVirtio->fMsiSupport)
    {
        pVirtio->uISR |= uCause;
        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_HIGH);
    }
    else if (uMsixVector != VIRTIO_MSI_NO_VECTOR)
        PDMDevHlpPCISetIrq(pDevIns, uMsixVector, 1);
    return VINF_SUCCESS;
}

/**
 * Lower interrupt. (Called when guest reads ISR)
 *
 * @param   pDevIns     The device instance.
 */
static void virtioLowerInterrupt(PPDMDEVINS pDevIns, uint16_t uMsixVector)
{
    PVIRTIOCORE pVirtio = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    if (!pVirtio->fMsiSupport)
        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_LOW);
    else if (uMsixVector != VIRTIO_MSI_NO_VECTOR)
        PDMDevHlpPCISetIrq(pDevIns, pVirtio->uMsixConfig, PDM_IRQ_LEVEL_LOW);
}

static void virtioResetQueue(PVIRTIOCORE pVirtio, uint16_t idxQueue)
{
    Assert(idxQueue < RT_ELEMENTS(pVirtio->virtqState));
    PVIRTQSTATE pVirtQ = &pVirtio->virtqState[idxQueue];
    pVirtQ->uAvailIdx = 0;
    pVirtQ->uUsedIdx  = 0;
    pVirtio->uQueueEnable[idxQueue] = false;
    pVirtio->uQueueSize[idxQueue] = VIRTQ_MAX_SIZE;
    pVirtio->uQueueNotifyOff[idxQueue] = idxQueue;

    pVirtio->uQueueMsixVector[idxQueue] = idxQueue + 2;
    if (!pVirtio->fMsiSupport) /* VirtIO 1.0, 4.1.4.3 and 4.1.5.1.2 */
        pVirtio->uQueueMsixVector[idxQueue] = VIRTIO_MSI_NO_VECTOR;
}

static void virtioResetDevice(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio)
{
    Log2Func(("\n"));
    pVirtio->uDeviceFeaturesSelect  = 0;
    pVirtio->uDriverFeaturesSelect  = 0;
    pVirtio->uConfigGeneration      = 0;
    pVirtio->uDeviceStatus          = 0;
    pVirtio->uISR                   = 0;

    if (!pVirtio->fMsiSupport)
        virtioLowerInterrupt(pDevIns, 0);
    else
    {
        virtioLowerInterrupt(pDevIns, pVirtio->uMsixConfig);
        for (int i = 0; i < VIRTQ_MAX_CNT; i++)
        {
            virtioLowerInterrupt(pDevIns, pVirtio->uQueueMsixVector[i]);
            pVirtio->uQueueMsixVector[i];
        }
    }

    if (!pVirtio->fMsiSupport)  /* VirtIO 1.0, 4.1.4.3 and 4.1.5.1.2 */
        pVirtio->uMsixConfig = VIRTIO_MSI_NO_VECTOR;

    for (uint16_t idxQueue = 0; idxQueue < VIRTQ_MAX_CNT; idxQueue++)
        virtioResetQueue(pVirtio, idxQueue);
}

/**
 * Initiate orderly reset procedure. This is an exposed API for clients that might need it.
 * Invoked by client to reset the device and driver (see VirtIO 1.0 section 2.1.1/2.1.2)
 */
void virtioCoreResetAll(PVIRTIOCORE pVirtio)
{
    LogFunc(("\n"));
    pVirtio->uDeviceStatus |= VIRTIO_STATUS_DEVICE_NEEDS_RESET;
    if (pVirtio->uDeviceStatus & VIRTIO_STATUS_DRIVER_OK)
    {
        pVirtio->fGenUpdatePending = true;
        virtioKick(pVirtio->pDevIns, pVirtio, VIRTIO_ISR_DEVICE_CONFIG, pVirtio->uMsixConfig, false /* fForce */);
    }
}

#ifdef IN_RING3
/**
 * Invoked by this implementation when guest driver resets the device.
 * The driver itself will not  until the device has read the status change.
 */
static void virtioGuestR3WasReset(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC)
{
    LogFunc(("Guest reset the device\n"));

    /* Let the client know */
    pVirtioCC->pfnStatusChanged(pVirtio, pVirtioCC, 0);
    virtioResetDevice(pDevIns, pVirtio);
}
#endif /* IN_RING3 */

/**
 * Handle accesses to Common Configuration capability
 *
 * @returns VBox status code
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pVirtioCC   Pointer to the current context virtio state.
 * @param   fWrite      Set if write access, clear if read access.
 * @param   offCfg      The common configuration capability offset.
 * @param   cb          Number of bytes to read or write
 * @param   pv          Pointer to location to write to or read from
 */
static int virtioCommonCfgAccessed(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC,
                                   int fWrite, uint32_t offCfg, unsigned cb, void *pv)
{
/**
 * This macro resolves to boolean true if the implied parameters, offCfg and cb,
 * match the field offset and size of a field in the Common Cfg struct, (or if
 * it is a 64-bit field, if it accesses either 32-bit part as a 32-bit access)
 * This is mandated by section 4.1.3.1 of the VirtIO 1.0 specification)
 *
 * @param   member  Member of VIRTIO_PCI_COMMON_CFG_T
 * @param   offCfg  Implied parameter: Offset into VIRTIO_PCI_COMMON_CFG_T
 * @param   cb      Implied parameter: Number of bytes to access
 * @result true or false
 */
#define MATCH_COMMON_CFG(member) \
    (   (   RT_SIZEOFMEMB(VIRTIO_PCI_COMMON_CFG_T, member) == 8 \
         && (   offCfg == RT_OFFSETOF(VIRTIO_PCI_COMMON_CFG_T, member) \
             || offCfg == RT_OFFSETOF(VIRTIO_PCI_COMMON_CFG_T, member) + sizeof(uint32_t)) \
         && cb == sizeof(uint32_t)) \
     || (   offCfg == RT_OFFSETOF(VIRTIO_PCI_COMMON_CFG_T, member) \
         && cb == RT_SIZEOFMEMB(VIRTIO_PCI_COMMON_CFG_T, member)) )

#ifdef LOG_ENABLED
# define LOG_COMMON_CFG_ACCESS(member, a_offIntra) \
    virtioCoreLogMappedIoValue(__FUNCTION__, #member, RT_SIZEOFMEMB(VIRTIO_PCI_COMMON_CFG_T, member), \
                           pv, cb, a_offIntra, fWrite, false, 0);
# define LOG_COMMON_CFG_ACCESS_INDEXED(member, idx, a_offIntra) \
    virtioCoreLogMappedIoValue(__FUNCTION__, #member, RT_SIZEOFMEMB(VIRTIO_PCI_COMMON_CFG_T, member), \
                           pv, cb, a_offIntra, fWrite, true, idx);
#else
# define LOG_COMMON_CFG_ACCESS(member, a_offIntra)              do { } while (0)
# define LOG_COMMON_CFG_ACCESS_INDEXED(member, idx, a_offIntra) do { } while (0)
#endif

#define COMMON_CFG_ACCESSOR(member) \
    do \
    { \
        uint32_t offIntra = offCfg - RT_OFFSETOF(VIRTIO_PCI_COMMON_CFG_T, member); \
        if (fWrite) \
            memcpy((char *)&pVirtio->member + offIntra, (const char *)pv, cb); \
        else \
            memcpy(pv, (const char *)&pVirtio->member + offIntra, cb); \
        LOG_COMMON_CFG_ACCESS(member, offIntra); \
    } while(0)

#define COMMON_CFG_ACCESSOR_INDEXED(member, idx) \
    do \
    { \
        uint32_t offIntra = offCfg - RT_OFFSETOF(VIRTIO_PCI_COMMON_CFG_T, member); \
        if (fWrite) \
            memcpy((char *)&pVirtio->member[idx] + offIntra, pv, cb); \
        else \
            memcpy(pv, (const char *)&pVirtio->member[idx] + offIntra, cb); \
        LOG_COMMON_CFG_ACCESS_INDEXED(member, idx, offIntra); \
    } while(0)

#define COMMON_CFG_ACCESSOR_READONLY(member) \
    do \
    { \
        uint32_t offIntra = offCfg - RT_OFFSETOF(VIRTIO_PCI_COMMON_CFG_T, member); \
        if (fWrite) \
            LogFunc(("Guest attempted to write readonly virtio_pci_common_cfg.%s\n", #member)); \
        else \
        { \
            memcpy(pv, (const char *)&pVirtio->member + offIntra, cb); \
            LOG_COMMON_CFG_ACCESS(member, offIntra); \
        } \
    } while(0)

#define COMMON_CFG_ACCESSOR_INDEXED_READONLY(member, idx) \
    do \
    { \
        uint32_t offIntra = offCfg - RT_OFFSETOF(VIRTIO_PCI_COMMON_CFG_T, member); \
        if (fWrite) \
            LogFunc(("Guest attempted to write readonly virtio_pci_common_cfg.%s[%d]\n", #member, idx)); \
        else \
        { \
            memcpy(pv, (char const *)&pVirtio->member[idx] + offIntra, cb); \
            LOG_COMMON_CFG_ACCESS_INDEXED(member, idx, offIntra); \
        } \
    } while(0)


    int rc = VINF_SUCCESS;
    uint64_t val;
    if (MATCH_COMMON_CFG(uDeviceFeatures))
    {
        if (fWrite) /* Guest WRITE pCommonCfg>uDeviceFeatures */
        {
            LogFunc(("Guest attempted to write readonly virtio_pci_common_cfg.device_feature\n"));
            return VINF_SUCCESS;
        }
        else /* Guest READ pCommonCfg->uDeviceFeatures */
        {
            switch (pVirtio->uDeviceFeaturesSelect)
            {
                case 0:
                    val = pVirtio->uDeviceFeatures & UINT32_C(0xffffffff);
                    memcpy(pv, &val, cb);
                    LOG_COMMON_CFG_ACCESS(uDeviceFeatures, offCfg - RT_UOFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDeviceFeatures));
                    break;
                case 1:
                    val = pVirtio->uDeviceFeatures >> 32;
                    memcpy(pv, &val, cb);
                    LOG_COMMON_CFG_ACCESS(uDeviceFeatures, offCfg - RT_UOFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDeviceFeatures) + 4);
                    break;
                default:
                    LogFunc(("Guest read uDeviceFeatures with out of range selector (%#x), returning 0\n",
                             pVirtio->uDeviceFeaturesSelect));
                    return VINF_IOM_MMIO_UNUSED_00;
            }
        }
    }
    else if (MATCH_COMMON_CFG(uDriverFeatures))
    {
        if (fWrite) /* Guest WRITE pCommonCfg->udriverFeatures */
        {
            switch (pVirtio->uDriverFeaturesSelect)
            {
                case 0:
                    memcpy(&pVirtio->uDriverFeatures, pv, cb);
                    LOG_COMMON_CFG_ACCESS(uDriverFeatures, offCfg - RT_UOFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDriverFeatures));
                    break;
                case 1:
                    memcpy((char *)&pVirtio->uDriverFeatures + sizeof(uint32_t), pv, cb);
                    LOG_COMMON_CFG_ACCESS(uDriverFeatures, offCfg - RT_UOFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDriverFeatures) + 4);
                    break;
                default:
                    LogFunc(("Guest wrote uDriverFeatures with out of range selector (%#x), returning 0\n",
                             pVirtio->uDriverFeaturesSelect));
                    return VINF_SUCCESS;
            }
        }
        else /* Guest READ pCommonCfg->udriverFeatures */
        {
            switch (pVirtio->uDriverFeaturesSelect)
            {
                case 0:
                    val = pVirtio->uDriverFeatures & 0xffffffff;
                    memcpy(pv, &val, cb);
                    LOG_COMMON_CFG_ACCESS(uDriverFeatures, offCfg - RT_UOFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDriverFeatures));
                    break;
                case 1:
                    val = (pVirtio->uDriverFeatures >> 32) & 0xffffffff;
                    memcpy(pv, &val, cb);
                    LOG_COMMON_CFG_ACCESS(uDriverFeatures, offCfg - RT_UOFFSETOF(VIRTIO_PCI_COMMON_CFG_T, uDriverFeatures) + 4);
                    break;
                default:
                    LogFunc(("Guest read uDriverFeatures with out of range selector (%#x), returning 0\n",
                             pVirtio->uDriverFeaturesSelect));
                    return VINF_IOM_MMIO_UNUSED_00;
            }
        }
    }
    else if (MATCH_COMMON_CFG(uNumQueues))
    {
        if (fWrite)
        {
            Log2Func(("Guest attempted to write readonly virtio_pci_common_cfg.num_queues\n"));
            return VINF_SUCCESS;
        }
        else
        {
            *(uint16_t *)pv = VIRTQ_MAX_CNT;
            LOG_COMMON_CFG_ACCESS(uNumQueues, 0);
        }
    }
    else if (MATCH_COMMON_CFG(uDeviceStatus))
    {
        if (fWrite) /* Guest WRITE pCommonCfg->uDeviceStatus */
        {
            uint8_t const fNewStatus = *(uint8_t *)pv;
            Log6Func(("Guest wrote uDeviceStatus (%#x, was %#x, change #%x) ................ (",
                      fNewStatus, pVirtio->uDeviceStatus, fNewStatus ^ pVirtio->uDeviceStatus));
            virtioLogDeviceStatus(fNewStatus);
            Log6((")\n"));

            /* If the status changed or we were reset, we need to go to ring-3 as
               it requires notifying the parent device. */
            bool const fStatusChanged =    (fNewStatus & VIRTIO_STATUS_DRIVER_OK)
                                        != (pVirtio->uPrevDeviceStatus & VIRTIO_STATUS_DRIVER_OK);
#ifndef IN_RING3
            if (fStatusChanged || fNewStatus == 0)
            {
                Log6Func(("=>ring3\n"));
                return VINF_IOM_R3_MMIO_WRITE;
            }
#endif
            pVirtio->uDeviceStatus = fNewStatus;

#ifdef IN_RING3
            /*
             * Notify client only if status actually changed from last time and when we're reset.
             */
            if (pVirtio->uDeviceStatus == 0)
                virtioGuestR3WasReset(pDevIns, pVirtio, pVirtioCC);
            if (fStatusChanged)
                pVirtioCC->pfnStatusChanged(pVirtio, pVirtioCC, fNewStatus & VIRTIO_STATUS_DRIVER_OK);
#endif
            /*
             * Save the current status for the next write so we can see what changed.
             */
            pVirtio->uPrevDeviceStatus = pVirtio->uDeviceStatus;
        }
        else /* Guest READ pCommonCfg->uDeviceStatus */
        {
            Log6Func(("Guest read  uDeviceStatus ................ ("));
            *(uint8_t *)pv = pVirtio->uDeviceStatus;
            virtioLogDeviceStatus(pVirtio->uDeviceStatus);
            Log6((")\n"));
        }
    }
    else
    if (MATCH_COMMON_CFG(uMsixConfig))
        COMMON_CFG_ACCESSOR(uMsixConfig);
    else
    if (MATCH_COMMON_CFG(uDeviceFeaturesSelect))
        COMMON_CFG_ACCESSOR(uDeviceFeaturesSelect);
    else
    if (MATCH_COMMON_CFG(uDriverFeaturesSelect))
        COMMON_CFG_ACCESSOR(uDriverFeaturesSelect);
    else
    if (MATCH_COMMON_CFG(uConfigGeneration))
        COMMON_CFG_ACCESSOR_READONLY(uConfigGeneration);
    else
    if (MATCH_COMMON_CFG(uQueueSelect))
        COMMON_CFG_ACCESSOR(uQueueSelect);
    else
    if (MATCH_COMMON_CFG(uQueueSize))
        COMMON_CFG_ACCESSOR_INDEXED(uQueueSize, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(uQueueMsixVector))
        COMMON_CFG_ACCESSOR_INDEXED(uQueueMsixVector, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(uQueueEnable))
        COMMON_CFG_ACCESSOR_INDEXED(uQueueEnable, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(uQueueNotifyOff))
        COMMON_CFG_ACCESSOR_INDEXED_READONLY(uQueueNotifyOff, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(aGCPhysQueueDesc))
        COMMON_CFG_ACCESSOR_INDEXED(aGCPhysQueueDesc, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(aGCPhysQueueAvail))
        COMMON_CFG_ACCESSOR_INDEXED(aGCPhysQueueAvail, pVirtio->uQueueSelect);
    else
    if (MATCH_COMMON_CFG(aGCPhysQueueUsed))
        COMMON_CFG_ACCESSOR_INDEXED(aGCPhysQueueUsed, pVirtio->uQueueSelect);
    else
    {
        Log2Func(("Bad guest %s access to virtio_pci_common_cfg: offCfg=%#x (%d), cb=%d\n",
                  fWrite ? "write" : "read ", offCfg, offCfg, cb));
        return fWrite ? VINF_SUCCESS : VINF_IOM_MMIO_UNUSED_00;
    }

#undef COMMON_CFG_ACCESSOR_READONLY
#undef COMMON_CFG_ACCESSOR_INDEXED_READONLY
#undef COMMON_CFG_ACCESSOR_INDEXED
#undef COMMON_CFG_ACCESSOR
#undef LOG_COMMON_CFG_ACCESS_INDEXED
#undef LOG_COMMON_CFG_ACCESS
#undef MATCH_COMMON_CFG
#ifndef IN_RING3
    RT_NOREF(pDevIns, pVirtioCC);
#endif
    return rc;
}

/**
 * @callback_method_impl{FNIOMMMIONEWREAD,
 * Memory mapped I/O Handler for PCI Capabilities read operations.}
 *
 * This MMIO handler specifically supports the VIRTIO_PCI_CAP_PCI_CFG capability defined
 * in the VirtIO 1.0 specification, section 4.1.4.7, and as such is limited to cb == 1, cb == 2, or cb==4 type reads.
 *
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PVIRTIOCORE   pVirtio   = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
    AssertReturn(cb == 1 || cb == 2 || cb == 4, VERR_INVALID_PARAMETER);
    Assert(pVirtio == (PVIRTIOCORE)pvUser); RT_NOREF(pvUser);

    uint32_t offIntra;
    if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, offIntra, pVirtio->LocDeviceCap))
    {
#ifdef IN_RING3
        /*
         * Callback to client to manage device-specific configuration.
         */
        VBOXSTRICTRC rcStrict = pVirtioCC->pfnDevCapRead(pDevIns, offIntra, pv, cb);

        /*
         * Additionally, anytime any part of the device-specific configuration (which our client maintains)
         * is READ it needs to be checked to see if it changed since the last time any part was read, in
         * order to maintain the config generation (see VirtIO 1.0 spec, section 4.1.4.3.1)
         */
        bool fDevSpecificFieldChanged = !!memcmp(pVirtioCC->pbDevSpecificCfg + offIntra,
                                                 pVirtioCC->pbPrevDevSpecificCfg + offIntra,
                                                 RT_MIN(cb, pVirtioCC->cbDevSpecificCfg - offIntra));

        memcpy(pVirtioCC->pbPrevDevSpecificCfg, pVirtioCC->pbDevSpecificCfg, pVirtioCC->cbDevSpecificCfg);

        if (pVirtio->fGenUpdatePending || fDevSpecificFieldChanged)
        {
            ++pVirtio->uConfigGeneration;
            Log6Func(("Bumped cfg. generation to %d because %s%s\n",
                      pVirtio->uConfigGeneration,
                      fDevSpecificFieldChanged ? "<dev cfg changed> " : "",
                      pVirtio->fGenUpdatePending ? "<update was pending>" : ""));
            pVirtio->fGenUpdatePending = false;
        }

        if (pVirtio->fMsiSupport)
            PDMDevHlpPCISetIrq(pDevIns, pVirtio->uMsixConfig, PDM_IRQ_LEVEL_LOW);

        return rcStrict;
#else
        return VINF_IOM_R3_MMIO_READ;
#endif
    }

    if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, offIntra, pVirtio->LocCommonCfgCap))
        return virtioCommonCfgAccessed(pDevIns, pVirtio, pVirtioCC, false /* fWrite */, offIntra, cb, pv);

    if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, offIntra, pVirtio->LocIsrCap) && cb == sizeof(uint8_t))
    {
        *(uint8_t *)pv = pVirtio->uISR;
        Log6Func(("Read and clear ISR\n"));
        pVirtio->uISR = 0; /* VirtIO specification requires reads of ISR to clear it */
        virtioLowerInterrupt(pDevIns, 0);
        return VINF_SUCCESS;
    }

    ASSERT_GUEST_MSG_FAILED(("Bad read access to mapped capabilities region: off=%RGp cb=%u\n", off, cb));
    return VINF_IOM_MMIO_UNUSED_00;
}

/**
 * @callback_method_impl{FNIOMMMIONEWREAD,
 * Memory mapped I/O Handler for PCI Capabilities write operations.}
 *
 * This MMIO handler specifically supports the VIRTIO_PCI_CAP_PCI_CFG capability defined
 * in the VirtIO 1.0 specification, section 4.1.4.7, and as such is limited to cb == 1, cb == 2, or cb==4 type writes.
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PVIRTIOCORE   pVirtio   = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);

    AssertReturn(cb == 1 || cb == 2 || cb == 4, VERR_INVALID_PARAMETER);

    Assert(pVirtio == (PVIRTIOCORE)pvUser); RT_NOREF(pvUser);

    uint32_t offIntra;
    if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, offIntra, pVirtio->LocDeviceCap))
    {
#ifdef IN_RING3
        /*
         * Pass this MMIO write access back to the client to handle
         */
        return pVirtioCC->pfnDevCapWrite(pDevIns, offIntra, pv, cb);
#else
        return VINF_IOM_R3_MMIO_WRITE;
#endif
    }

    if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, offIntra, pVirtio->LocCommonCfgCap))
        return virtioCommonCfgAccessed(pDevIns, pVirtio, pVirtioCC, true /* fWrite */, offIntra, cb, (void *)pv);

    if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, offIntra, pVirtio->LocIsrCap) && cb == sizeof(uint8_t))
    {
        pVirtio->uISR = *(uint8_t *)pv;
        Log6Func(("Setting uISR = 0x%02x (virtq interrupt: %d, dev confg interrupt: %d)\n",
                  pVirtio->uISR & 0xff,
                  pVirtio->uISR & VIRTIO_ISR_VIRTQ_INTERRUPT,
                  RT_BOOL(pVirtio->uISR & VIRTIO_ISR_DEVICE_CONFIG)));
        return VINF_SUCCESS;
    }

    /* This *should* be guest driver dropping index of a new descriptor in avail ring */
    if (MATCHES_VIRTIO_CAP_STRUCT(off, cb, offIntra, pVirtio->LocNotifyCap) && cb == sizeof(uint16_t))
    {
#ifdef IN_RING3
        virtioR3QueueNotified(pVirtio, pVirtioCC, offIntra / VIRTIO_NOTIFY_OFFSET_MULTIPLIER, *(uint16_t *)pv);
        return VINF_SUCCESS;
#else
        return VINF_IOM_R3_MMIO_WRITE;
#endif
    }

    ASSERT_GUEST_MSG_FAILED(("Bad write access to mapped capabilities region: off=%RGp pv=%#p{%.*Rhxs} cb=%u\n", off, pv, cb, pv, cb));
    return VINF_SUCCESS;
}

#ifdef IN_RING3

/**
 * @callback_method_impl{FNPCICONFIGREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioR3PciConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                        uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    PVIRTIOCORE   pVirtio   = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
    RT_NOREF(pPciDev);

    LogFlowFunc(("pDevIns=%p pPciDev=%p uAddress=%#x cb=%u pu32Value=%p\n",
                 pDevIns, pPciDev, uAddress, cb, pu32Value));
    if (uAddress == pVirtio->uPciCfgDataOff)
    {
        /*
         * VirtIO 1.0 spec section 4.1.4.7 describes a required alternative access capability
         * whereby the guest driver can specify a bar, offset, and length via the PCI configuration space
         * (the virtio_pci_cfg_cap capability), and access data items.
         */
        uint32_t uLength = pVirtioCC->pPciCfgCap->pciCap.uLength;
        uint32_t uOffset = pVirtioCC->pPciCfgCap->pciCap.uOffset;
        uint8_t  uBar    = pVirtioCC->pPciCfgCap->pciCap.uBar;

        if (   (uLength != 1 && uLength != 2 && uLength != 4)
            || cb != uLength
            || uBar != VIRTIO_REGION_PCI_CAP)
        {
            ASSERT_GUEST_MSG_FAILED(("Guest read virtio_pci_cfg_cap.pci_cfg_data using mismatching config. Ignoring\n"));
            *pu32Value = UINT32_MAX;
            return VINF_SUCCESS;
        }

        VBOXSTRICTRC rcStrict = virtioMmioRead(pDevIns, pVirtio, uOffset, pu32Value, cb);
        Log2Func(("virtio: Guest read  virtio_pci_cfg_cap.pci_cfg_data, bar=%d, offset=%d, length=%d, result=%d -> %Rrc\n",
                  uBar, uOffset, uLength, *pu32Value, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }
    return VINF_PDM_PCI_DO_DEFAULT;
}

/**
 * @callback_method_impl{FNPCICONFIGWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) virtioR3PciConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                         uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    PVIRTIOCORE   pVirtio   = PDMINS_2_DATA(pDevIns, PVIRTIOCORE);
    PVIRTIOCORECC pVirtioCC = PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC);
    RT_NOREF(pPciDev);

    LogFlowFunc(("pDevIns=%p pPciDev=%p uAddress=%#x cb=%u u32Value=%#x\n", pDevIns, pPciDev, uAddress, cb, u32Value));
    if (uAddress == pVirtio->uPciCfgDataOff)
    {
        /* VirtIO 1.0 spec section 4.1.4.7 describes a required alternative access capability
         * whereby the guest driver can specify a bar, offset, and length via the PCI configuration space
         * (the virtio_pci_cfg_cap capability), and access data items. */

        uint32_t uLength = pVirtioCC->pPciCfgCap->pciCap.uLength;
        uint32_t uOffset = pVirtioCC->pPciCfgCap->pciCap.uOffset;
        uint8_t  uBar    = pVirtioCC->pPciCfgCap->pciCap.uBar;

        if (   (uLength != 1 && uLength != 2 && uLength != 4)
            || cb != uLength
            || uBar != VIRTIO_REGION_PCI_CAP)
        {
            ASSERT_GUEST_MSG_FAILED(("Guest write virtio_pci_cfg_cap.pci_cfg_data using mismatching config. Ignoring\n"));
            return VINF_SUCCESS;
        }

        VBOXSTRICTRC rcStrict = virtioMmioWrite(pDevIns, pVirtio, uOffset, &u32Value, cb);
        Log2Func(("Guest wrote  virtio_pci_cfg_cap.pci_cfg_data, bar=%d, offset=%x, length=%x, value=%d -> %Rrc\n",
                  uBar, uOffset, uLength, u32Value, VBOXSTRICTRC_VAL(rcStrict)));
        return rcStrict;
    }
    return VINF_PDM_PCI_DO_DEFAULT;
}


/*********************************************************************************************************************************
*   Saved state.                                                                                                                 *
*********************************************************************************************************************************/

/**
 * Called from the FNSSMDEVSAVEEXEC function of the device.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pHlp        The ring-3 device helpers.
 * @param   pSSM        The saved state handle.
 * @returns VBox status code.
 */
int virtioCoreR3SaveExec(PVIRTIOCORE pVirtio, PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM)
{
    LogFunc(("\n"));
    pHlp->pfnSSMPutU64(pSSM, VIRTIO_SAVEDSTATE_MARKER);
    pHlp->pfnSSMPutU32(pSSM, VIRTIO_SAVEDSTATE_VERSION);

    pHlp->pfnSSMPutBool(pSSM,   pVirtio->fGenUpdatePending);
    pHlp->pfnSSMPutU8(pSSM,     pVirtio->uDeviceStatus);
    pHlp->pfnSSMPutU8(pSSM,     pVirtio->uConfigGeneration);
    pHlp->pfnSSMPutU8(pSSM,     pVirtio->uPciCfgDataOff);
    pHlp->pfnSSMPutU8(pSSM,     pVirtio->uISR);
    pHlp->pfnSSMPutU16(pSSM,    pVirtio->uQueueSelect);
    pHlp->pfnSSMPutU32(pSSM,    pVirtio->uDeviceFeaturesSelect);
    pHlp->pfnSSMPutU32(pSSM,    pVirtio->uDriverFeaturesSelect);
    pHlp->pfnSSMPutU64(pSSM,    pVirtio->uDriverFeatures);

    for (uint32_t i = 0; i < VIRTQ_MAX_CNT; i++)
    {
        pHlp->pfnSSMPutGCPhys64(pSSM, pVirtio->aGCPhysQueueDesc[i]);
        pHlp->pfnSSMPutGCPhys64(pSSM, pVirtio->aGCPhysQueueAvail[i]);
        pHlp->pfnSSMPutGCPhys64(pSSM, pVirtio->aGCPhysQueueUsed[i]);
        pHlp->pfnSSMPutU16(pSSM,      pVirtio->uQueueNotifyOff[i]);
        pHlp->pfnSSMPutU16(pSSM,      pVirtio->uQueueMsixVector[i]);
        pHlp->pfnSSMPutU16(pSSM,      pVirtio->uQueueEnable[i]);
        pHlp->pfnSSMPutU16(pSSM,      pVirtio->uQueueSize[i]);
        pHlp->pfnSSMPutU16(pSSM,      pVirtio->virtqState[i].uAvailIdx);
        pHlp->pfnSSMPutU16(pSSM,      pVirtio->virtqState[i].uUsedIdx);
        int rc = pHlp->pfnSSMPutMem(pSSM, pVirtio->virtqState[i].szVirtqName, 32);
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}

/**
 * Called from the FNSSMDEVLOADEXEC function of the device.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pHlp        The ring-3 device helpers.
 * @param   pSSM        The saved state handle.
 * @returns VBox status code.
 */
int virtioCoreR3LoadExec(PVIRTIOCORE pVirtio, PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM)
{
    LogFunc(("\n"));
    /*
     * Check the marker and (embedded) version number.
     */
    uint64_t uMarker = 0;
    int rc = pHlp->pfnSSMGetU64(pSSM, &uMarker);
    AssertRCReturn(rc, rc);
    if (uMarker != VIRTIO_SAVEDSTATE_MARKER)
        return pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                        N_("Expected marker value %#RX64 found %#RX64 instead"),
                                        VIRTIO_SAVEDSTATE_MARKER, uMarker);
    uint32_t uVersion = 0;
    rc = pHlp->pfnSSMGetU32(pSSM, &uVersion);
    AssertRCReturn(rc, rc);
    if (uVersion != VIRTIO_SAVEDSTATE_VERSION)
        return pHlp->pfnSSMSetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                        N_("Unsupported virtio version: %u"), uVersion);
    /*
     * Load the state.
     */
    pHlp->pfnSSMGetBool(pSSM, &pVirtio->fGenUpdatePending);
    pHlp->pfnSSMGetU8(pSSM,   &pVirtio->uDeviceStatus);
    pHlp->pfnSSMGetU8(pSSM,   &pVirtio->uConfigGeneration);
    pHlp->pfnSSMGetU8(pSSM,   &pVirtio->uPciCfgDataOff);
    pHlp->pfnSSMGetU8(pSSM,   &pVirtio->uISR);
    pHlp->pfnSSMGetU16(pSSM,  &pVirtio->uQueueSelect);
    pHlp->pfnSSMGetU32(pSSM,  &pVirtio->uDeviceFeaturesSelect);
    pHlp->pfnSSMGetU32(pSSM,  &pVirtio->uDriverFeaturesSelect);
    pHlp->pfnSSMGetU64(pSSM,  &pVirtio->uDriverFeatures);

    for (uint32_t idxQueue = 0; idxQueue < VIRTQ_MAX_CNT; idxQueue++)
    {
        pHlp->pfnSSMGetGCPhys64(pSSM, &pVirtio->aGCPhysQueueDesc[idxQueue]);
        pHlp->pfnSSMGetGCPhys64(pSSM, &pVirtio->aGCPhysQueueAvail[idxQueue]);
        pHlp->pfnSSMGetGCPhys64(pSSM, &pVirtio->aGCPhysQueueUsed[idxQueue]);
        pHlp->pfnSSMGetU16(pSSM, &pVirtio->uQueueNotifyOff[idxQueue]);
        pHlp->pfnSSMGetU16(pSSM, &pVirtio->uQueueMsixVector[idxQueue]);
        pHlp->pfnSSMGetU16(pSSM, &pVirtio->uQueueEnable[idxQueue]);
        pHlp->pfnSSMGetU16(pSSM, &pVirtio->uQueueSize[idxQueue]);
        pHlp->pfnSSMGetU16(pSSM, &pVirtio->virtqState[idxQueue].uAvailIdx);
        pHlp->pfnSSMGetU16(pSSM, &pVirtio->virtqState[idxQueue].uUsedIdx);
        rc = pHlp->pfnSSMGetMem(pSSM, pVirtio->virtqState[idxQueue].szVirtqName,
                                sizeof(pVirtio->virtqState[idxQueue].szVirtqName));
        AssertRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   Device Level                                                                                                                 *
*********************************************************************************************************************************/

/**
 * This should be called from PDMDEVREGR3::pfnReset.
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 */
void virtioCoreR3VmStateChanged(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, VIRTIOVMSTATECHANGED enmState)
{

    LogFunc(("State changing to %s\n",
        virtioCoreGetStateChangeText(enmState)));

    switch(enmState)
    {
        case kvirtIoVmStateChangedReset:
            virtioCoreResetAll(pVirtio);
            break;
        case kvirtIoVmStateChangedSuspend:
            break;
        case kvirtIoVmStateChangedPowerOff:
            break;
        case kvirtIoVmStateChangedResume:
            virtioNotifyGuestDriver(pVirtio->pDevIns, pVirtio, 0 /* idxQueue */, true /* fForce */);
            break;
        default:
            LogRelFunc(("Bad enum value"));
            return;
    }
    RT_NOREF(pDevIns, pVirtio);
}

/**
 * This should be called from PDMDEVREGR3::pfnDestruct.
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pVirtioCC   Pointer to the ring-3 virtio state.
 */
void virtioCoreR3Term(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC)
{
    if (pVirtioCC->pbPrevDevSpecificCfg)
    {
        RTMemFree(pVirtioCC->pbPrevDevSpecificCfg);
        pVirtioCC->pbPrevDevSpecificCfg = NULL;
    }
    RT_NOREF(pDevIns, pVirtio);
}


/**
 * Setup PCI device controller and Virtio state
 *
 * This should be called from PDMDEVREGR3::pfnConstruct.
 *
 * @param   pDevIns                 The device instance.
 * @param   pVirtio                 Pointer to the shared virtio state.  This
 *                                  must be the first member in the shared
 *                                  device instance data!
 * @param   pVirtioCC               Pointer to the ring-3 virtio state.  This
 *                                  must be the first member in the ring-3
 *                                  device instance data!
 * @param   pPciParams              Values to populate industry standard PCI Configuration Space data structure
 * @param   pcszInstance            Device instance name (format-specifier)
 * @param   fDevSpecificFeatures    VirtIO device-specific features offered by
 *                                  client
 * @param   cbDevSpecificCfg        Size of virtio_pci_device_cap device-specific struct
 * @param   pvDevSpecificCfg        Address of client's dev-specific
 *                                  configuration struct.
 */
int virtioCoreR3Init(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC, PVIRTIOPCIPARAMS pPciParams,
                     const char *pcszInstance, uint64_t fDevSpecificFeatures, void *pvDevSpecificCfg, uint16_t cbDevSpecificCfg)
{
    /*
     * The pVirtio state must be the first member of the shared device instance
     * data, otherwise we cannot get our bearings in the PCI configuration callbacks.
     */
    AssertLogRelReturn(pVirtio == PDMINS_2_DATA(pDevIns, PVIRTIOCORE), VERR_STATE_CHANGED);
    AssertLogRelReturn(pVirtioCC == PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC), VERR_STATE_CHANGED);

    pVirtio->pDevIns = pDevIns;

    /*
     * Caller must initialize these.
     */
    AssertReturn(pVirtioCC->pfnStatusChanged, VERR_INVALID_POINTER);
    AssertReturn(pVirtioCC->pfnQueueNotified, VERR_INVALID_POINTER);
    AssertReturn(pVirtioCC->pfnDevCapRead,    VERR_INVALID_POINTER);
    AssertReturn(pVirtioCC->pfnDevCapWrite,   VERR_INVALID_POINTER);

#if 0 /* Until pdmR3DvHlp_PCISetIrq() impl is fixed and Assert that limits vec to 0 is removed */
# ifdef VBOX_WITH_MSI_DEVICES
    pVirtio->fMsiSupport = true;
# endif
#endif

    /*
     * The host features offered include both device-specific features
     * and reserved feature bits (device independent)
     */
    pVirtio->uDeviceFeatures = VIRTIO_F_VERSION_1
                             | VIRTIO_DEV_INDEPENDENT_FEATURES_OFFERED
                             | fDevSpecificFeatures;

    RTStrCopy(pVirtio->szInstance, sizeof(pVirtio->szInstance), pcszInstance);

    pVirtio->uDeviceStatus = 0;
    pVirtioCC->cbDevSpecificCfg = cbDevSpecificCfg;
    pVirtioCC->pbDevSpecificCfg = (uint8_t *)pvDevSpecificCfg;
    pVirtioCC->pbPrevDevSpecificCfg = (uint8_t *)RTMemDup(pvDevSpecificCfg, cbDevSpecificCfg);
    AssertLogRelReturn(pVirtioCC->pbPrevDevSpecificCfg, VERR_NO_MEMORY);

    /* Set PCI config registers (assume 32-bit mode) */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetRevisionId(pPciDev,         DEVICE_PCI_REVISION_ID_VIRTIO);
    PDMPciDevSetVendorId(pPciDev,           DEVICE_PCI_VENDOR_ID_VIRTIO);
    PDMPciDevSetSubSystemVendorId(pPciDev,  DEVICE_PCI_VENDOR_ID_VIRTIO);
    PDMPciDevSetDeviceId(pPciDev,           pPciParams->uDeviceId);
    PDMPciDevSetClassBase(pPciDev,          pPciParams->uClassBase);
    PDMPciDevSetClassSub(pPciDev,           pPciParams->uClassSub);
    PDMPciDevSetClassProg(pPciDev,          pPciParams->uClassProg);
    PDMPciDevSetSubSystemId(pPciDev,        pPciParams->uSubsystemId);
    PDMPciDevSetInterruptLine(pPciDev,      pPciParams->uInterruptLine);
    PDMPciDevSetInterruptPin(pPciDev,       pPciParams->uInterruptPin);

    /* Register PCI device */
    int rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio: cannot register PCI Device")); /* can we put params in this error? */

    rc = PDMDevHlpPCIInterceptConfigAccesses(pDevIns, pPciDev, virtioR3PciConfigRead, virtioR3PciConfigWrite);
    AssertRCReturn(rc, rc);


    /* Construct & map PCI vendor-specific capabilities for virtio host negotiation with guest driver */

    /* The following capability mapped via VirtIO 1.0: struct virtio_pci_cfg_cap (VIRTIO_PCI_CFG_CAP_T)
     * as a mandatory but suboptimal alternative interface to host device capabilities, facilitating
     * access the memory of any BAR. If the guest uses it (the VirtIO driver on Linux doesn't),
     * Unlike Common, Notify, ISR and Device capabilities, it is accessed directly via PCI Config region.
     * therefore does not contribute to the capabilities region (BAR) the other capabilities use.
     */
#define CFG_ADDR_2_IDX(addr) ((uint8_t)(((uintptr_t)(addr) - (uintptr_t)&pPciDev->abConfig[0])))
#define SET_PCI_CAP_LOC(a_pPciDev, a_pCfg, a_LocCap, a_uMmioLengthAlign) \
        do { \
            (a_LocCap).offMmio = (a_pCfg)->uOffset; \
            (a_LocCap).cbMmio  = RT_ALIGN_T((a_pCfg)->uLength, a_uMmioLengthAlign, uint16_t); \
            (a_LocCap).offPci  = (uint16_t)(uintptr_t)((uint8_t *)(a_pCfg) - &(a_pPciDev)->abConfig[0]); \
            (a_LocCap).cbPci   = (a_pCfg)->uCapLen; \
        } while (0)

    PVIRTIO_PCI_CAP_T pCfg;
    uint32_t cbRegion = 0;

    /* Common capability (VirtIO 1.0 spec, section 4.1.4.3) */
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[0x40];
    pCfg->uCfgType = VIRTIO_PCI_CAP_COMMON_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
    pCfg->uCapNext = CFG_ADDR_2_IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
    pCfg->uOffset  = RT_ALIGN_32(0, 4); /* reminder, in case someone changes offset */
    pCfg->uLength  = sizeof(VIRTIO_PCI_COMMON_CFG_T);
    cbRegion += pCfg->uLength;
    SET_PCI_CAP_LOC(pPciDev, pCfg, pVirtio->LocCommonCfgCap, 2);
    pVirtioCC->pCommonCfgCap = pCfg;

    /*
     * Notify capability (VirtIO 1.0 spec, section 4.1.4.4). Note: uLength is based the choice
     * of this implementation that each queue's uQueueNotifyOff is set equal to (QueueSelect) ordinal
     * value of the queue */
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_NOTIFY_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_NOTIFY_CAP_T);
    pCfg->uCapNext = CFG_ADDR_2_IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
    pCfg->uOffset  = pVirtioCC->pCommonCfgCap->uOffset + pVirtioCC->pCommonCfgCap->uLength;
    pCfg->uOffset  = RT_ALIGN_32(pCfg->uOffset, 4);


    pCfg->uLength  = VIRTQ_MAX_CNT * VIRTIO_NOTIFY_OFFSET_MULTIPLIER + 2;  /* will change in VirtIO 1.1 */
    cbRegion += pCfg->uLength;
    SET_PCI_CAP_LOC(pPciDev, pCfg, pVirtio->LocNotifyCap, 1);
    pVirtioCC->pNotifyCap = (PVIRTIO_PCI_NOTIFY_CAP_T)pCfg;
    pVirtioCC->pNotifyCap->uNotifyOffMultiplier = VIRTIO_NOTIFY_OFFSET_MULTIPLIER;

    /* ISR capability (VirtIO 1.0 spec, section 4.1.4.5)
     *
     * VirtIO 1.0 spec says 8-bit, unaligned in MMIO space. Example/diagram
     * of spec shows it as a 32-bit field with upper bits 'reserved'
     * Will take spec words more literally than the diagram for now.
     */
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_ISR_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
    pCfg->uCapNext = CFG_ADDR_2_IDX(pCfg) + pCfg->uCapLen;
    pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
    pCfg->uOffset  = pVirtioCC->pNotifyCap->pciCap.uOffset + pVirtioCC->pNotifyCap->pciCap.uLength;
    pCfg->uOffset  = RT_ALIGN_32(pCfg->uOffset, 4);
    pCfg->uLength  = sizeof(uint8_t);
    cbRegion += pCfg->uLength;
    SET_PCI_CAP_LOC(pPciDev, pCfg, pVirtio->LocIsrCap, 4);
    pVirtioCC->pIsrCap = pCfg;

    /*  PCI Cfg capability (VirtIO 1.0 spec, section 4.1.4.7)
     *  This capability doesn't get page-MMIO mapped. Instead uBar, uOffset and uLength are intercepted
     *  by trapping PCI configuration I/O and get modulated by consumers to locate fetch and read/write
     *  values from any region. NOTE: The linux driver not only doesn't use this feature, it will not
     *  even list it as present if uLength isn't non-zero and 4-byte-aligned as the linux driver is
     *  initializing. */

    pVirtio->uPciCfgDataOff = pCfg->uCapNext + RT_OFFSETOF(VIRTIO_PCI_CFG_CAP_T, uPciCfgData);
    pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
    pCfg->uCfgType = VIRTIO_PCI_CAP_PCI_CFG;
    pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
    pCfg->uCapLen  = sizeof(VIRTIO_PCI_CFG_CAP_T);
    pCfg->uCapNext = (pVirtio->fMsiSupport || pVirtioCC->pbDevSpecificCfg) ? CFG_ADDR_2_IDX(pCfg) + pCfg->uCapLen : 0;
    pCfg->uBar     = 0;
    pCfg->uOffset  = 0;
    pCfg->uLength  = 0;
    cbRegion += pCfg->uLength;
    SET_PCI_CAP_LOC(pPciDev, pCfg, pVirtio->LocPciCfgCap, 1);
    pVirtioCC->pPciCfgCap = (PVIRTIO_PCI_CFG_CAP_T)pCfg;

    if (pVirtioCC->pbDevSpecificCfg)
    {
        /* Following capability (via VirtIO 1.0, section 4.1.4.6). Client defines the
         * device-specific config fields struct and passes size to this constructor */
        pCfg = (PVIRTIO_PCI_CAP_T)&pPciDev->abConfig[pCfg->uCapNext];
        pCfg->uCfgType = VIRTIO_PCI_CAP_DEVICE_CFG;
        pCfg->uCapVndr = VIRTIO_PCI_CAP_ID_VENDOR;
        pCfg->uCapLen  = sizeof(VIRTIO_PCI_CAP_T);
        pCfg->uCapNext = pVirtio->fMsiSupport ? CFG_ADDR_2_IDX(pCfg) + pCfg->uCapLen : 0;
        pCfg->uBar     = VIRTIO_REGION_PCI_CAP;
        pCfg->uOffset  = pVirtioCC->pIsrCap->uOffset + pVirtioCC->pIsrCap->uLength;
        pCfg->uOffset  = RT_ALIGN_32(pCfg->uOffset, 4);
        pCfg->uLength  = cbDevSpecificCfg;
        cbRegion += pCfg->uLength;
        SET_PCI_CAP_LOC(pPciDev, pCfg, pVirtio->LocDeviceCap, 4);
        pVirtioCC->pDeviceCap = pCfg;
    }
    else
        Assert(pVirtio->LocDeviceCap.cbMmio == 0 && pVirtio->LocDeviceCap.cbPci == 0);

    if (pVirtio->fMsiSupport)
    {
        PDMMSIREG aMsiReg;
        RT_ZERO(aMsiReg);
        aMsiReg.iMsixCapOffset  = pCfg->uCapNext;
        aMsiReg.iMsixNextOffset = 0;
        aMsiReg.iMsixBar        = VIRTIO_REGION_MSIX_CAP;
        aMsiReg.cMsixVectors    = VBOX_MSIX_MAX_ENTRIES;
        rc = PDMDevHlpPCIRegisterMsi(pDevIns, &aMsiReg); /* see MsixR3init() */
        if (RT_FAILURE(rc))
        {
            /* See PDMDevHlp.cpp:pdmR3DevHlp_PCIRegisterMsi */
            LogFunc(("Failed to configure MSI-X (%Rrc). Reverting to INTx\n", rc));
            pVirtio->fMsiSupport = false;
        }
        else
            Log2Func(("Using MSI-X for guest driver notification\n"));
    }
    else
        LogFunc(("MSI-X not available for VBox, using INTx notification\n"));


    /* Set offset to first capability and enable PCI dev capabilities */
    PDMPciDevSetCapabilityList(pPciDev, 0x40);
    PDMPciDevSetStatus(pPciDev,         VBOX_PCI_STATUS_CAP_LIST);

    /* Linux drivers/virtio/virtio_pci_modern.c tries to map at least a page for the
     * 'unknown' device-specific capability without querying the capability to figure
     *  out size, so pad with an extra page */

    rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, VIRTIO_REGION_PCI_CAP, RT_ALIGN_32(cbRegion + PAGE_SIZE, PAGE_SIZE),
                                        PCI_ADDRESS_SPACE_MEM, virtioMmioWrite, virtioMmioRead, pVirtio,
                                        IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU, "virtio-scsi MMIO",
                                        &pVirtio->hMmioPciCap);
    AssertLogRelRCReturn(rc, PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio: cannot register PCI Capabilities address space")));

    return rc;
}

#else  /* !IN_RING3 */

/**
 * Sets up the core ring-0/raw-mode virtio bits.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.  This must be the first
 *                      member in the shared device instance data!
 * @param   pVirtioCC   Pointer to the current context virtio state.  This must be the
 *                      first member in the currenct context's device instance data!
 */
int virtioCoreRZInit(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC)
{
    AssertLogRelReturn(pVirtio == PDMINS_2_DATA(pDevIns, PVIRTIOCORE), VERR_STATE_CHANGED);
    AssertLogRelReturn(pVirtioCC == PDMINS_2_DATA_CC(pDevIns, PVIRTIOCORECC), VERR_STATE_CHANGED);

    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pVirtio->hMmioPciCap, virtioMmioWrite, virtioMmioRead, pVirtio);
    AssertRCReturn(rc, rc);
    return rc;
}

#endif /* !IN_RING3 */

