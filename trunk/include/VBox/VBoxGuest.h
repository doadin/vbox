/** @file
 * VBoxGuest - VirtualBox Guest Additions interface
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ___VBox_VBoxGuest_h
#define ___VBox_VBoxGuest_h

#include <iprt/cdefs.h>
#include <iprt/types.h>
#ifdef IN_RING3
# include <iprt/stdarg.h>
#endif
#include <VBox/err.h>
#include <VBox/ostypes.h>

/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/

/** @todo The following is a temporary fix for the problem of accessing
    hypervisor pointers from within guest additions */

/** Hypervisor linear pointer size type */
typedef RTGCPTR32 VMMDEVHYPPTR32;
typedef RTGCPTR64 VMMDEVHYPPTR64;
/** Hypervisor physical pointer size type */
typedef RTGCPHYS32 VMMDEVHYPPHYS32;
typedef RTGCPHYS64 VMMDEVHYPPHYS64;

#if defined(VBOX_WITH_64_BITS_GUESTS) && ARCH_BITS == 64
# define VMMDEVHYPPTR  VMMDEVHYPPTR64
# define VMMDEVHYPPHYS VMMDEVHYPPHYS64
# else
# define VMMDEVHYPPTR  VMMDEVHYPPTR32
# define VMMDEVHYPPHYS VMMDEVHYPPHYS32
#endif

#if defined(RT_OS_LINUX)
/** The support device name. */
# define VBOXGUEST_DEVICE_NAME        "/dev/vboxadd"

#elif defined(RT_OS_OS2)
/** The support device name. */
# define VBOXGUEST_DEVICE_NAME        "\\Dev\\VBoxGst$"

#elif defined(RT_OS_SOLARIS)
/** The support device name. */
# define VBOXGUEST_DEVICE_NAME        "/dev/vboxguest"

#elif defined(RT_OS_WINDOWS)
/** The support service name. */
# define VBOXGUEST_SERVICE_NAME       "VBoxGuest"
/** Win32 Device name. */
# define VBOXGUEST_DEVICE_NAME        "\\\\.\\VBoxGuest"
/** Global name for Win2k+ */
# define VBOXGUEST_DEVICE_NAME_GLOBAL "\\\\.\\Global\\VBoxGuest"
/** Win32 driver name */
# define VBOXGUEST_DEVICE_NAME_NT     L"\\Device\\VBoxGuest"
/** device name */
# define VBOXGUEST_DEVICE_NAME_DOS    L"\\DosDevices\\VBoxGuest"

#elif defined(RT_OS_FREEBSD)
/** The support device name. */
# define VBOXGUEST_DEVICE_NAME        "/dev/vboxguest"

#else
/* PORTME */
#endif

/** VirtualBox vendor ID */
#define VBOX_PCI_VENDORID (0x80ee)

/** VMMDev PCI card identifiers */
#define VMMDEV_VENDORID VBOX_PCI_VENDORID
#define VMMDEV_DEVICEID (0xcafe)

/** VirtualBox graphics card identifiers */
#define VBOX_VENDORID VBOX_PCI_VENDORID
#define VBOX_VESA_VENDORID VBOX_PCI_VENDORID
#define VBOX_DEVICEID (0xbeef)
#define VBOX_VESA_DEVICEID (0xbeef)

/**
 * VBoxGuest port definitions
 * @{
 */

/** guest can (== wants to) handle absolute coordinates */
#define VBOXGUEST_MOUSE_GUEST_CAN_ABSOLUTE      RT_BIT(0)
/** host can (== wants to) send absolute coordinates */
#define VBOXGUEST_MOUSE_HOST_CAN_ABSOLUTE       RT_BIT(1)
/** guest can *NOT* switch to software cursor and therefore depends on the host cursor */
#define VBOXGUEST_MOUSE_GUEST_NEEDS_HOST_CURSOR RT_BIT(2)
/** host does NOT provide support for drawing the cursor itself (e.g. L4 console) */
#define VBOXGUEST_MOUSE_HOST_CANNOT_HWPOINTER   RT_BIT(3)

/** fictive start address of the hypervisor physical memory for MmMapIoSpace */
#define HYPERVISOR_PHYSICAL_START  0xf8000000

/*
 * VMMDev Generic Request Interface
 */

/** port for generic request interface */
#define PORT_VMMDEV_REQUEST_OFFSET 0

/** Current version of the VMMDev interface.
 *
 * Additions are allowed to work only if
 * additions_major == vmmdev_current && additions_minor <= vmmdev_current.
 * Additions version is reported to host (VMMDev) by VMMDevReq_ReportGuestInfo.
 *
 * @remark  These defines also live in the 16-bit and assembly versions of this header.
 */
#define VMMDEV_VERSION       0x00010004
#define VMMDEV_VERSION_MAJOR (VMMDEV_VERSION >> 16)
#define VMMDEV_VERSION_MINOR (VMMDEV_VERSION & 0xffff)

/* Maximum request packet size */
#define VMMDEV_MAX_VMMDEVREQ_SIZE           _1M

/**
 * VMMDev request types.
 * @note when updating this, adjust vmmdevGetRequestSize() as well
 */
typedef enum
{
    VMMDevReq_InvalidRequest             =  0,
    VMMDevReq_GetMouseStatus             =  1,
    VMMDevReq_SetMouseStatus             =  2,
    VMMDevReq_SetPointerShape            =  3,
    /** @todo implement on host side */
    VMMDevReq_GetHostVersion             =  4,
    VMMDevReq_Idle                       =  5,
    VMMDevReq_GetHostTime                = 10,
    VMMDevReq_GetHypervisorInfo          = 20,
    VMMDevReq_SetHypervisorInfo          = 21,
    VMMDevReq_SetPowerStatus             = 30,
    VMMDevReq_AcknowledgeEvents          = 41,
    VMMDevReq_CtlGuestFilterMask         = 42,
    VMMDevReq_ReportGuestInfo            = 50,
    VMMDevReq_GetDisplayChangeRequest    = 51,
    VMMDevReq_VideoModeSupported         = 52,
    VMMDevReq_GetHeightReduction         = 53,
    VMMDevReq_GetDisplayChangeRequest2   = 54,
    VMMDevReq_ReportGuestCapabilities    = 55,
    VMMDevReq_SetGuestCapabilities       = 56,
#ifdef VBOX_WITH_HGCM
    VMMDevReq_HGCMConnect                = 60,
    VMMDevReq_HGCMDisconnect             = 61,
#ifdef VBOX_WITH_64_BITS_GUESTS
    VMMDevReq_HGCMCall32                 = 62,
    VMMDevReq_HGCMCall64                 = 63,
#else
    VMMDevReq_HGCMCall                   = 62,
#endif /* VBOX_WITH_64_BITS_GUESTS */
    VMMDevReq_HGCMCancel                 = 64,
#endif
    VMMDevReq_VideoAccelEnable           = 70,
    VMMDevReq_VideoAccelFlush            = 71,
    VMMDevReq_VideoSetVisibleRegion      = 72,
    VMMDevReq_GetSeamlessChangeRequest   = 73,
    VMMDevReq_QueryCredentials           = 100,
    VMMDevReq_ReportCredentialsJudgement = 101,
    VMMDevReq_ReportGuestStats           = 110,
    VMMDevReq_GetMemBalloonChangeRequest = 111,
    VMMDevReq_GetStatisticsChangeRequest = 112,
    VMMDevReq_ChangeMemBalloon           = 113,
    VMMDevReq_GetVRDPChangeRequest       = 150,
    VMMDevReq_LogString                  = 200,
    VMMDevReq_SizeHack                   = 0x7fffffff
} VMMDevRequestType;

#ifdef VBOX_WITH_64_BITS_GUESTS
/*
 * Constants and structures are redefined for the guest.
 *
 * Host code MUST always use either *32 or *64 variant explicitely.
 * Host source code will use VBOX_HGCM_HOST_CODE define to catch undefined
 * data types and constants.
 *
 * This redefinition means that the new additions builds will use
 * the *64 or *32 variants depending on the current architecture bit count (ARCH_BITS).
 */
# ifndef VBOX_HGCM_HOST_CODE
#  if ARCH_BITS == 64
#   define VMMDevReq_HGCMCall VMMDevReq_HGCMCall64
#  elif ARCH_BITS == 32
#   define VMMDevReq_HGCMCall VMMDevReq_HGCMCall32
#  else
#   error "Unsupported ARCH_BITS"
#  endif
# endif /* !VBOX_HGCM_HOST_CODE */
#endif /* VBOX_WITH_64_BITS_GUESTS */

/** Version of VMMDevRequestHeader structure. */
#define VMMDEV_REQUEST_HEADER_VERSION (0x10001)

#pragma pack(4)
/** generic VMMDev request header */
typedef struct
{
    /** size of the structure in bytes (including body). Filled by caller */
    uint32_t size;
    /** version of the structure. Filled by caller */
    uint32_t version;
    /** type of the request */
    VMMDevRequestType requestType;
    /** return code. Filled by VMMDev */
    int32_t  rc;
    /** reserved fields */
    uint32_t reserved1;
    uint32_t reserved2;
} VMMDevRequestHeader;

/** mouse status request structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** mouse feature mask */
    uint32_t mouseFeatures;
    /** mouse x position */
    uint32_t pointerXPos;
    /** mouse y position */
    uint32_t pointerYPos;
} VMMDevReqMouseStatus;

/** Note VBOX_MOUSE_POINTER_* flags are used in guest video driver,
 *  values must be <= 0x8000 and must not be changed.
 */

/** pointer is visible */
#define VBOX_MOUSE_POINTER_VISIBLE (0x0001)
/** pointer has alpha channel */
#define VBOX_MOUSE_POINTER_ALPHA   (0x0002)
/** pointerData contains new pointer shape */
#define VBOX_MOUSE_POINTER_SHAPE   (0x0004)

/** mouse pointer shape/visibility change request */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** VBOX_MOUSE_POINTER_* bit flags */
    uint32_t fFlags;
    /** x coordinate of hot spot */
    uint32_t xHot;
    /** y coordinate of hot spot */
    uint32_t yHot;
    /** width of the pointer in pixels */
    uint32_t width;
    /** height of the pointer in scanlines */
    uint32_t height;
    /** Pointer data.
     *
     ****
     * The data consists of 1 bpp AND mask followed by 32 bpp XOR (color) mask.
     *
     * For pointers without alpha channel the XOR mask pixels are 32 bit values: (lsb)BGR0(msb).
     * For pointers with alpha channel the XOR mask consists of (lsb)BGRA(msb) 32 bit values.
     *
     * Guest driver must create the AND mask for pointers with alpha channel, so if host does not
     * support alpha, the pointer could be displayed as a normal color pointer. The AND mask can
     * be constructed from alpha values. For example alpha value >= 0xf0 means bit 0 in the AND mask.
     *
     * The AND mask is 1 bpp bitmap with byte aligned scanlines. Size of AND mask,
     * therefore, is cbAnd = (width + 7) / 8 * height. The padding bits at the
     * end of any scanline are undefined.
     *
     * The XOR mask follows the AND mask on the next 4 bytes aligned offset:
     * uint8_t *pXor = pAnd + (cbAnd + 3) & ~3
     * Bytes in the gap between the AND and the XOR mask are undefined.
     * XOR mask scanlines have no gap between them and size of XOR mask is:
     * cXor = width * 4 * height.
     ****
     *
     * Preallocate 4 bytes for accessing actual data as p->pointerData
     */
    char pointerData[4];
} VMMDevReqMousePointer;

/** string log request structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** variable length string data */
    char szString[1];
} VMMDevReqLogString;

/** host version request structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** major version */
    uint32_t major;
    /** minor version */
    uint32_t minor;
    /** build number */
    uint32_t build;
} VMMDevReqHostVersion;

/** guest capabilites structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** capabilities (VMMDEV_GUEST_*) */
    uint32_t    caps;
} VMMDevReqGuestCapabilities;

/** guest capabilites structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** mask of capabilities to be added */
    uint32_t    u32OrMask;
    /** mask of capabilities to be removed */
    uint32_t    u32NotMask;
} VMMDevReqGuestCapabilities2;

/** idle request structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
} VMMDevReqIdle;

/** host time request structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** time in milliseconds since unix epoch. Filled by VMMDev. */
    uint64_t time;
} VMMDevReqHostTime;

/** hypervisor info structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** guest virtual address of proposed hypervisor start */
    /** TODO: Make this 64-bit compatible */
    VMMDEVHYPPTR32 hypervisorStart;
    /** hypervisor size in bytes */
    uint32_t hypervisorSize;
} VMMDevReqHypervisorInfo;

/** system power requests */
typedef enum
{
    VMMDevPowerState_Invalid   = 0,
    VMMDevPowerState_Pause     = 1,
    VMMDevPowerState_PowerOff  = 2,
    VMMDevPowerState_SaveState = 3,
    VMMDevPowerState_SizeHack = 0x7fffffff
} VMMDevPowerState;

/** system power status structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** power state request */
    VMMDevPowerState powerState;
} VMMDevPowerStateRequest;

/** pending events structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** pending event bitmap */
    uint32_t events;
} VMMDevEvents;

/** guest filter mask control */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** mask of events to be added to filter */
    uint32_t u32OrMask;
    /** mask of events to be removed from filter */
    uint32_t u32NotMask;
} VMMDevCtlGuestFilterMask;

/** guest information structure */
typedef struct VBoxGuestInfo
{
    /** The VMMDev interface version expected by additions. */
    uint32_t additionsVersion;
    /** guest OS type */
    VBOXOSTYPE osType;
    /** @todo */
} VBoxGuestInfo;

/** guest information structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** Guest information. */
    VBoxGuestInfo guestInfo;
} VMMDevReportGuestInfo;

/** guest statistics values */
#define VBOX_GUEST_STAT_CPU_LOAD_IDLE       RT_BIT(0)
#define VBOX_GUEST_STAT_CPU_LOAD_KERNEL     RT_BIT(1)
#define VBOX_GUEST_STAT_CPU_LOAD_USER       RT_BIT(2)
#define VBOX_GUEST_STAT_THREADS             RT_BIT(3)
#define VBOX_GUEST_STAT_PROCESSES           RT_BIT(4)
#define VBOX_GUEST_STAT_HANDLES             RT_BIT(5)
#define VBOX_GUEST_STAT_MEMORY_LOAD         RT_BIT(6)
#define VBOX_GUEST_STAT_PHYS_MEM_TOTAL      RT_BIT(7)
#define VBOX_GUEST_STAT_PHYS_MEM_AVAIL      RT_BIT(8)
#define VBOX_GUEST_STAT_PHYS_MEM_BALLOON    RT_BIT(9)
#define VBOX_GUEST_STAT_MEM_COMMIT_TOTAL    RT_BIT(10)
#define VBOX_GUEST_STAT_MEM_KERNEL_TOTAL    RT_BIT(11)
#define VBOX_GUEST_STAT_MEM_KERNEL_PAGED    RT_BIT(12)
#define VBOX_GUEST_STAT_MEM_KERNEL_NONPAGED RT_BIT(13)
#define VBOX_GUEST_STAT_MEM_SYSTEM_CACHE    RT_BIT(14)
#define VBOX_GUEST_STAT_PAGE_FILE_SIZE      RT_BIT(15)


/** guest statistics structure */
typedef struct VBoxGuestStatistics
{
    /** Virtual CPU id */
    uint32_t        u32CpuId;
    /** Reported statistics */
    uint32_t        u32StatCaps;
    /** Idle CPU load (0-100) for last interval */
    uint32_t        u32CpuLoad_Idle;
    /** Kernel CPU load (0-100) for last interval */
    uint32_t        u32CpuLoad_Kernel;
    /** User CPU load (0-100) for last interval */
    uint32_t        u32CpuLoad_User;
    /** Nr of threads */
    uint32_t        u32Threads;
    /** Nr of processes */
    uint32_t        u32Processes;
    /** Nr of handles */
    uint32_t        u32Handles;
    /** Memory load (0-100) */
    uint32_t        u32MemoryLoad;
    /** Page size of guest system */
    uint32_t        u32PageSize;
    /** Total physical memory (in 4kb pages) */
    uint32_t        u32PhysMemTotal;
    /** Available physical memory (in 4kb pages) */
    uint32_t        u32PhysMemAvail;
    /** Ballooned physical memory (in 4kb pages) */
    uint32_t        u32PhysMemBalloon;
    /** Total number of committed memory (which is not necessarily in-use) (in 4kb pages) */
    uint32_t        u32MemCommitTotal;
    /** Total amount of memory used by the kernel (in 4kb pages) */
    uint32_t        u32MemKernelTotal;
    /** Total amount of paged memory used by the kernel (in 4kb pages) */
    uint32_t        u32MemKernelPaged;
    /** Total amount of nonpaged memory used by the kernel (in 4kb pages) */
    uint32_t        u32MemKernelNonPaged;
    /** Total amount of memory used for the system cache (in 4kb pages) */
    uint32_t        u32MemSystemCache;
    /** Pagefile size (in 4kb pages) */
    uint32_t        u32PageFileSize;
} VBoxGuestStatistics;

/** guest statistics command structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** Guest information. */
    VBoxGuestStatistics guestStats;
} VMMDevReportGuestStats;

/** memory balloon change request structure */
#define VMMDEV_MAX_MEMORY_BALLOON(PhysMemTotal)     ((90*PhysMemTotal)/100)

typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    uint32_t            u32BalloonSize;     /* balloon size in megabytes */
    uint32_t            u32PhysMemSize;     /* guest ram size in megabytes */
    uint32_t            eventAck;
} VMMDevGetMemBalloonChangeRequest;

/** inflate/deflate memory balloon structure */
#define VMMDEV_MEMORY_BALLOON_CHUNK_PAGES            (_1M/4096)
#define VMMDEV_MEMORY_BALLOON_CHUNK_SIZE             (VMMDEV_MEMORY_BALLOON_CHUNK_PAGES*4096)

typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    uint32_t            cPages;
    uint32_t            fInflate;       /* true = inflate, false = defalte */
    /** Physical address (RTGCPHYS) of each page, variable size. */
    RTGCPHYS            aPhysPage[1];
} VMMDevChangeMemBalloon;

/** guest statistics interval change request structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    uint32_t            u32StatInterval; /* interval in seconds */
    uint32_t            eventAck;
} VMMDevGetStatisticsChangeRequest;

/** display change request structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** horizontal pixel resolution (0 = do not change) */
    uint32_t xres;
    /** vertical pixel resolution (0 = do not change) */
    uint32_t yres;
    /** bits per pixel (0 = do not change) */
    uint32_t bpp;
    /** Flag that the request is an acknowlegement for the VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST.
     *  Values: 0 - just querying, VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST - event acknowledged.
     */
    uint32_t eventAck;
} VMMDevDisplayChangeRequest;

typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** horizontal pixel resolution (0 = do not change) */
    uint32_t xres;
    /** vertical pixel resolution (0 = do not change) */
    uint32_t yres;
    /** bits per pixel (0 = do not change) */
    uint32_t bpp;
    /** Flag that the request is an acknowlegement for the VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST.
     *  Values: 0 - just querying, VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST - event acknowledged.
     */
    uint32_t eventAck;
    /** 0 for primary display, 1 for the first secondary, etc. */
    uint32_t display;
} VMMDevDisplayChangeRequest2;

/** video mode supported request structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** horizontal pixel resolution (input) */
    uint32_t width;
    /** vertical pixel resolution (input) */
    uint32_t height;
    /** bits per pixel (input) */
    uint32_t bpp;
    /** supported flag (output) */
    bool fSupported;
} VMMDevVideoModeSupportedRequest;

/** video modes height reduction request structure */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** height reduction in pixels (output) */
    uint32_t heightReduction;
} VMMDevGetHeightReductionRequest;

#define VRDP_EXPERIENCE_LEVEL_ZERO     0 /* Theming disabled. */
#define VRDP_EXPERIENCE_LEVEL_LOW      1 /* Full window dragging and desktop wallpaper disabled. */
#define VRDP_EXPERIENCE_LEVEL_MEDIUM   2 /* Font smoothing, gradients. */
#define VRDP_EXPERIENCE_LEVEL_HIGH     3 /* Animation effects disabled. */
#define VRDP_EXPERIENCE_LEVEL_FULL     4 /* Everything enabled. */

typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** Whether VRDP is active or not */
    uint8_t u8VRDPActive;
    /** The configured experience level for active VRDP. */
    uint32_t u32VRDPExperienceLevel;
} VMMDevVRDPChangeRequest;



#pragma pack()

#ifdef VBOX_WITH_HGCM

/** HGCM flags.
 *  @{
 */
#define VBOX_HGCM_REQ_DONE      (0x1)
#define VBOX_HGCM_REQ_CANCELLED (0x2)
/** @} */

#pragma pack(4)
typedef struct _VMMDevHGCMRequestHeader
{
    /** Request header. */
    VMMDevRequestHeader header;

    /** HGCM flags. */
    uint32_t fu32Flags;

    /** Result code. */
    int32_t result;
} VMMDevHGCMRequestHeader;

/** HGCM service location types. */
typedef enum
{
    VMMDevHGCMLoc_Invalid    = 0,
    VMMDevHGCMLoc_LocalHost  = 1,
    VMMDevHGCMLoc_LocalHost_Existing = 2,
    VMMDevHGCMLoc_SizeHack   = 0x7fffffff
} HGCMServiceLocationType;

typedef struct
{
    char achName[128]; /**< This is really szName. */
} HGCMServiceLocationHost;

typedef struct HGCMSERVICELOCATION
{
    /** Type of the location. */
    HGCMServiceLocationType type;

    union
    {
        HGCMServiceLocationHost host;
    } u;
} HGCMServiceLocation;

typedef struct
{
    /* request header */
    VMMDevHGCMRequestHeader header;

    /** IN: Description of service to connect to. */
    HGCMServiceLocation loc;

    /** OUT: Client identifier assigned by local instance of HGCM. */
    uint32_t u32ClientID;
} VMMDevHGCMConnect;

typedef struct
{
    /* request header */
    VMMDevHGCMRequestHeader header;

    /** IN: Client identifier. */
    uint32_t u32ClientID;
} VMMDevHGCMDisconnect;

typedef enum
{
    VMMDevHGCMParmType_Invalid            = 0,
    VMMDevHGCMParmType_32bit              = 1,
    VMMDevHGCMParmType_64bit              = 2,
    VMMDevHGCMParmType_PhysAddr           = 3,
    VMMDevHGCMParmType_LinAddr            = 4, /**< In and Out */
    VMMDevHGCMParmType_LinAddr_In         = 5, /**< In  (read;  host<-guest) */
    VMMDevHGCMParmType_LinAddr_Out        = 6, /**< Out (write; host->guest) */
    VMMDevHGCMParmType_LinAddr_Locked     = 7, /**< Locked In and Out */
    VMMDevHGCMParmType_LinAddr_Locked_In  = 8, /**< Locked In  (read;  host<-guest) */
    VMMDevHGCMParmType_LinAddr_Locked_Out = 9, /**< Locked Out (write; host->guest) */
    VMMDevHGCMParmType_SizeHack           = 0x7fffffff
} HGCMFunctionParameterType;

#ifdef VBOX_WITH_64_BITS_GUESTS
typedef struct _HGCMFUNCTIONPARAMETER32
{
    HGCMFunctionParameterType type;
    union
    {
        uint32_t   value32;
        uint64_t   value64;
        struct
        {
            uint32_t size;

            union
            {
                VMMDEVHYPPHYS32 physAddr;
                VMMDEVHYPPTR32  linearAddr;
            } u;
        } Pointer;
    } u;
#ifdef __cplusplus
    void SetUInt32(uint32_t u32)
    {
        type = VMMDevHGCMParmType_32bit;
        u.value64 = 0; /* init unused bits to 0 */
        u.value32 = u32;
    }

    int GetUInt32(uint32_t *pu32)
    {
        if (type == VMMDevHGCMParmType_32bit)
        {
            *pu32 = u.value32;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetUInt64(uint64_t u64)
    {
        type      = VMMDevHGCMParmType_64bit;
        u.value64 = u64;
    }

    int GetUInt64(uint64_t *pu64)
    {
        if (type == VMMDevHGCMParmType_64bit)
        {
            *pu64 = u.value64;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetPtr(void *pv, uint32_t cb)
    {
        type                    = VMMDevHGCMParmType_LinAddr;
        u.Pointer.size          = cb;
        u.Pointer.u.linearAddr  = (VMMDEVHYPPTR32)(uintptr_t)pv;
    }
#endif
} HGCMFunctionParameter32;

typedef struct _HGCMFUNCTIONPARAMETER64
{
    HGCMFunctionParameterType type;
    union
    {
        uint32_t   value32;
        uint64_t   value64;
        struct
        {
            uint32_t size;

            union
            {
                VMMDEVHYPPHYS64 physAddr;
                VMMDEVHYPPTR64  linearAddr;
            } u;
        } Pointer;
    } u;
#ifdef __cplusplus
    void SetUInt32(uint32_t u32)
    {
        type = VMMDevHGCMParmType_32bit;
        u.value64 = 0; /* init unused bits to 0 */
        u.value32 = u32;
    }

    int GetUInt32(uint32_t *pu32)
    {
        if (type == VMMDevHGCMParmType_32bit)
        {
            *pu32 = u.value32;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetUInt64(uint64_t u64)
    {
        type      = VMMDevHGCMParmType_64bit;
        u.value64 = u64;
    }

    int GetUInt64(uint64_t *pu64)
    {
        if (type == VMMDevHGCMParmType_64bit)
        {
            *pu64 = u.value64;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetPtr(void *pv, uint32_t cb)
    {
        type                    = VMMDevHGCMParmType_LinAddr;
        u.Pointer.size          = cb;
        u.Pointer.u.linearAddr  = (uintptr_t)pv;
    }
#endif
} HGCMFunctionParameter64;
#else /* !VBOX_WITH_64_BITS_GUESTS */
typedef struct _HGCMFUNCTIONPARAMETER
{
    HGCMFunctionParameterType type;
    union
    {
        uint32_t   value32;
        uint64_t   value64;
        struct
        {
            uint32_t size;

            union
            {
                VMMDEVHYPPHYS32 physAddr;
                VMMDEVHYPPTR32  linearAddr;
            } u;
        } Pointer;
    } u;
#ifdef __cplusplus
    void SetUInt32(uint32_t u32)
    {
        type = VMMDevHGCMParmType_32bit;
        u.value64 = 0; /* init unused bits to 0 */
        u.value32 = u32;
    }

    int GetUInt32(uint32_t *pu32)
    {
        if (type == VMMDevHGCMParmType_32bit)
        {
            *pu32 = u.value32;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetUInt64(uint64_t u64)
    {
        type      = VMMDevHGCMParmType_64bit;
        u.value64 = u64;
    }

    int GetUInt64(uint64_t *pu64)
    {
        if (type == VMMDevHGCMParmType_64bit)
        {
            *pu64 = u.value64;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetPtr(void *pv, uint32_t cb)
    {
        type                    = VMMDevHGCMParmType_LinAddr;
        u.Pointer.size          = cb;
        u.Pointer.u.linearAddr  = (uintptr_t)pv;
    }
#endif
} HGCMFunctionParameter;
#endif /* !VBOX_WITH_64_BITS_GUESTS */


#ifdef VBOX_WITH_64_BITS_GUESTS
/* Redefine the structure type for the guest code. */
# ifndef VBOX_HGCM_HOST_CODE
#  if ARCH_BITS == 64
#    define HGCMFunctionParameter HGCMFunctionParameter64
#  elif ARCH_BITS == 32
#    define HGCMFunctionParameter HGCMFunctionParameter32
#  else
#   error "Unsupported sizeof (void *)"
#  endif
# endif /* !VBOX_HGCM_HOST_CODE */
#endif /* VBOX_WITH_64_BITS_GUESTS */

typedef struct
{
    /* request header */
    VMMDevHGCMRequestHeader header;

    /** IN: Client identifier. */
    uint32_t u32ClientID;
    /** IN: Service function number. */
    uint32_t u32Function;
    /** IN: Number of parameters. */
    uint32_t cParms;
    /** Parameters follow in form: HGCMFunctionParameter aParms[X]; */
} VMMDevHGCMCall;
#pragma pack()

#define VMMDEV_HGCM_CALL_PARMS(a)   ((HGCMFunctionParameter *)((uint8_t *)a + sizeof (VMMDevHGCMCall)))
#define VMMDEV_HGCM_CALL_PARMS32(a) ((HGCMFunctionParameter32 *)((uint8_t *)a + sizeof (VMMDevHGCMCall)))

#ifdef VBOX_WITH_64_BITS_GUESTS
/* Explicit defines for the host code. */
# ifdef VBOX_HGCM_HOST_CODE
#  define VMMDEV_HGCM_CALL_PARMS32(a) ((HGCMFunctionParameter32 *)((uint8_t *)a + sizeof (VMMDevHGCMCall)))
#  define VMMDEV_HGCM_CALL_PARMS64(a) ((HGCMFunctionParameter64 *)((uint8_t *)a + sizeof (VMMDevHGCMCall)))
# endif /* VBOX_HGCM_HOST_CODE */
#endif /* VBOX_WITH_64_BITS_GUESTS */

#define VBOX_HGCM_MAX_PARMS 32

/* The Cancel request is issued using the same physical memory address
 * as was used for the corresponding initial HGCMCall.
 */
typedef struct
{
    /* request header */
    VMMDevHGCMRequestHeader header;
} VMMDevHGCMCancel;

#endif /* VBOX_WITH_HGCM */


#define VBVA_F_STATUS_ACCEPTED (0x01)
#define VBVA_F_STATUS_ENABLED  (0x02)

#pragma pack(4)

typedef struct _VMMDevVideoAccelEnable
{
    /* request header */
    VMMDevRequestHeader header;

    /** 0 - disable, !0 - enable. */
    uint32_t u32Enable;

    /** The size of VBVAMEMORY::au8RingBuffer expected by driver.
     *  The host will refuse to enable VBVA if the size is not equal to
     *  VBVA_RING_BUFFER_SIZE.
     */
    uint32_t cbRingBuffer;

    /** Guest initializes the status to 0. Host sets appropriate VBVA_F_STATUS_ flags. */
    uint32_t fu32Status;

} VMMDevVideoAccelEnable;

typedef struct _VMMDevVideoAccelFlush
{
    /* request header */
    VMMDevRequestHeader header;

} VMMDevVideoAccelFlush;


typedef struct _VMMDevVideoSetVisibleRegion
{
    /* request header */
    VMMDevRequestHeader header;

    /** Number of rectangles */
    uint32_t cRect;

    /** Rectangle array */
    RTRECT   Rect;
} VMMDevVideoSetVisibleRegion;


/** Seamless mode */
typedef enum
{
    VMMDev_Seamless_Disabled         = 0,     /* normal mode; entire guest desktop displayed */
    VMMDev_Seamless_Visible_Region   = 1,     /* visible region mode; only top-level guest windows displayed */
    VMMDev_Seamless_Host_Window      = 2      /* windowed mode; each top-level guest window is represented in a host window */
} VMMDevSeamlessMode;

typedef struct
{
    /** header */
    VMMDevRequestHeader header;

    /** New seamless mode */
    VMMDevSeamlessMode  mode;
    /** Flag that the request is an acknowlegement for the VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST.
     *  Values: 0 - just querying, VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST - event acknowledged.
     */
    uint32_t eventAck;
} VMMDevSeamlessChangeRequest;

#pragma pack()

#pragma pack(1)
/** VBVA command header. */
typedef struct _VBVACMDHDR
{
   /** Coordinates of affected rectangle. */
   int16_t x;
   int16_t y;
   uint16_t w;
   uint16_t h;
} VBVACMDHDR;
#pragma pack()

/* The VBVA ring buffer is suitable for transferring large (< 2gb) amount of data.
 * For example big bitmaps which do not fit to the buffer.
 *
 * Guest starts writing to the buffer by initializing a record entry in the
 * aRecords queue. VBVA_F_RECORD_PARTIAL indicates that the record is being
 * written. As data is written to the ring buffer, the guest increases off32End
 * for the record.
 *
 * The host reads the aRecords on flushes and processes all completed records.
 * When host encounters situation when only a partial record presents and
 * cbRecord & ~VBVA_F_RECORD_PARTIAL >= VBVA_RING_BUFFER_SIZE - VBVA_RING_BUFFER_THRESHOLD,
 * the host fetched all record data and updates off32Head. After that on each flush
 * the host continues fetching the data until the record is completed.
 *
 */

#define VBVA_RING_BUFFER_SIZE        (_4M - _1K)
#define VBVA_RING_BUFFER_THRESHOLD   (4 * _1K)

#define VBVA_MAX_RECORDS (64)

#define VBVA_F_MODE_ENABLED         (0x00000001)
#define VBVA_F_MODE_VRDP            (0x00000002)
#define VBVA_F_MODE_VRDP_RESET      (0x00000004)
#define VBVA_F_MODE_VRDP_ORDER_MASK (0x00000008)

#define VBVA_F_RECORD_PARTIAL   (0x80000000)

#pragma pack(1)
typedef struct _VBVARECORD
{
    /** The length of the record. Changed by guest. */
    uint32_t cbRecord;
} VBVARECORD;

typedef struct _VBVAMEMORY
{
    /** VBVA_F_MODE_* */
    uint32_t fu32ModeFlags;

    /** The offset where the data start in the buffer. */
    uint32_t off32Data;
    /** The offset where next data must be placed in the buffer. */
    uint32_t off32Free;

    /** The ring buffer for data. */
    uint8_t  au8RingBuffer[VBVA_RING_BUFFER_SIZE];

    /** The queue of record descriptions. */
    VBVARECORD aRecords[VBVA_MAX_RECORDS];
    uint32_t indexRecordFirst;
    uint32_t indexRecordFree;

    /* RDP orders supported by the client. The guest reports only them
     * and falls back to DIRTY rects for not supported ones.
     *
     * (1 << VBVA_VRDP_*)
     */
    uint32_t fu32SupportedOrders;

} VBVAMEMORY;
#pragma pack()

/** @} */


/**
 * VMMDev RAM
 * @{
 */

#pragma pack(1)
/** Layout of VMMDEV RAM region that contains information for guest */
typedef struct
{
    /** size */
    uint32_t u32Size;
    /** version */
    uint32_t u32Version;

    union {
        /** Flag telling that VMMDev set the IRQ and acknowlegment is required */
        struct {
            bool fHaveEvents;
        } V1_04;

        struct {
            /** Pending events flags, set by host. */
            uint32_t u32HostEvents;
            /** Mask of events the guest wants to see, set by guest. */
            uint32_t u32GuestEventMask;
        } V1_03;
    } V;

    VBVAMEMORY vbvaMemory;

} VMMDevMemory;
#pragma pack()

/** Version of VMMDevMemory structure. */
#define VMMDEV_MEMORY_VERSION (1)

/** @} */


/**
 * VMMDev events.
 * @{
 */

/** Host mouse capabilities has been changed. */
#define VMMDEV_EVENT_MOUSE_CAPABILITIES_CHANGED             RT_BIT(0)
/** HGCM event. */
#define VMMDEV_EVENT_HGCM                                   RT_BIT(1)
/** A display change request has been issued. */
#define VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST                 RT_BIT(2)
/** Credentials are available for judgement. */
#define VMMDEV_EVENT_JUDGE_CREDENTIALS                      RT_BIT(3)
/** The guest has been restored. */
#define VMMDEV_EVENT_RESTORED                               RT_BIT(4)
/** Seamless mode state changed */
#define VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST           RT_BIT(5)
/** Memory balloon size changed */
#define VMMDEV_EVENT_BALLOON_CHANGE_REQUEST                 RT_BIT(6)
/** Statistics interval changed */
#define VMMDEV_EVENT_STATISTICS_INTERVAL_CHANGE_REQUEST     RT_BIT(7)
/** VRDP status changed. */
#define VMMDEV_EVENT_VRDP                                   RT_BIT(8)
/** New mouse position data available */
#define VMMDEV_EVENT_MOUSE_POSITION_CHANGED                 RT_BIT(9)

/** @} */


#if !defined(IN_RC) && !defined(IN_RING0_AGNOSTIC) && !defined(IPRT_NO_CRT)
/** @name VBoxGuest IOCTL codes and structures.
 *
 * The range 0..15 is for basic driver communication.
 * The range 16..31 is for HGCM communcation.
 * The range 32..47 is reserved for future use.
 * The range 48..63 is for OS specific communcation.
 * The 7th bit is reserved for future hacks.
 * The 8th bit is reserved for distinguishing between 32-bit and 64-bit
 * processes in future 64-bit guest additions.
 *
 * While windows IOCTL function number has to start at 2048 and stop at 4096 there
 * never was any need to do this for everyone. A simple ((Function) | 0x800) would
 * have sufficed. On Linux we're now intruding upon the type field. Fortunately
 * this hasn't caused any trouble because the FILE_DEVICE_UNKNOWN value was set
 * to 0x22 (if it were 0x2C it would not have worked soo smoothly). The situation
 * would've been the same for *BSD and Darwin since they seems to share common
 * _IOC() heritage.
 *
 * However, on good old OS/2 we only have 8-bit handy for the function number. The
 * result from using the old IOCTL function numbers her would've been overlapping
 * between the two ranges.
 *
 * To fix this problem and get rid of all the unnecessary windowsy crap that I
 * bet was copied from my SUPDRVIOC.h once upon a time (although the concept of
 * prefixing macros with the purpose of avoid clashes with system stuff and
 * to indicate exactly how owns them seems to have been lost somewhere along
 * the way), I've introduced a VBOXGUEST_IOCTL_CODE for defining generic IN/OUT
 * IOCtls on new ports of the additions.
 *
 * @remarks When creating new IOCtl interfaces keep in mind that not all OSes supports
 *          reporting back the output size. (This got messed up a little bit in VBoxDrv.)
 *
 *          The request size is also a little bit tricky as it's passed as part of the
 *          request code on unix. The size field is 14 bits on Linux, 12 bits on *BSD,
 *          13 bits Darwin, and 8-bits on Solaris. All the BSDs and Darwin kernels
 *          will make use of the size field, while Linux and Solaris will not. We're of
 *          course using the size to validate and/or map/lock the request, so it has
 *          to be valid.
 *
 *          For Solaris we will have to do something special though, 255 isn't
 *          sufficent for all we need. A 4KB restriction (BSD) is probably not
 *          too problematic (yet) as a general one.
 *
 *          More info can be found in SUPDRVIOC.h and related sources.
 *
 * @remarks If adding interfaces that only has input or only has output, some new macros
 *          needs to be created so the most efficient IOCtl data buffering method can be
 *          used.
 * @{
 */
#ifdef RT_ARCH_AMD64
# define VBOXGUEST_IOCTL_FLAG     128
#elif defined(RT_ARCH_X86)
# define VBOXGUEST_IOCTL_FLAG     0
#else
# error "dunno which arch this is!"
#endif

/** Ring-3 request wrapper for big requests.
 *
 * This is necessary because the ioctl number scheme on many Unixy OSes (esp. Solaris)
 * only allows a relatively small size to be encoded into the request. So, for big
 * request this generic form is used instead. */
typedef struct VBGLBIGREQ
{
    /** Magic value (VBGLBIGREQ_MAGIC). */
    uint32_t    u32Magic;
    /** The size of the data buffer. */
    uint32_t    cbData;
    /** The user address of the data buffer. */
    RTR3PTR     pvDataR3;
} VBGLBIGREQ;
/** Pointer to a request wrapper for solaris guests. */
typedef VBGLBIGREQ *PVBGLBIGREQ;
/** Pointer to a const request wrapper for solaris guests. */
typedef const VBGLBIGREQ *PCVBGLBIGREQ;

/** The VBGLBIGREQ::u32Magic value (Ryuu Murakami). */
#define VBGLBIGREQ_MAGIC                        0x19520219


#if defined(RT_OS_WINDOWS)
/* @todo Remove IOCTL_CODE later! Integrate it in VBOXGUEST_IOCTL_CODE below. */
/** @todo r=bird: IOCTL_CODE is supposedly defined in some header included by Windows.h or ntddk.h, which is why it wasn't in the #if 0 earlier. See HostDrivers/Support/SUPDrvIOC.h... */
# define IOCTL_CODE(DeviceType, Function, Method, Access, DataSize_ignored) \
  ( ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
# define VBOXGUEST_IOCTL_CODE(Function, Size)   IOCTL_CODE(FILE_DEVICE_UNKNOWN, 2048 + (Function), METHOD_BUFFERED, FILE_WRITE_ACCESS, 0)
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)       (Code)

#elif defined(RT_OS_OS2)
  /* No automatic buffering, size not encoded. */
# define VBOXGUEST_IOCTL_CATEGORY               0xc2
# define VBOXGUEST_IOCTL_CODE(Function, Size)   ((unsigned char)(Function))
# define VBOXGUEST_IOCTL_CATEGORY_FAST          0xc3 /**< Also defined in VBoxGuestA-os2.asm. */
# define VBOXGUEST_IOCTL_CODE_FAST(Function)    ((unsigned char)(Function))
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)       (Code)

#elif defined(RT_OS_SOLARIS)
  /* No automatic buffering, size limited to 255 bytes => use VBGLBIGREQ for everything. */
# include <sys/ioccom.h>
# define VBOXGUEST_IOCTL_CODE(Function, Size)   _IOWRN('V', (Function) | VBOXGUEST_IOCTL_FLAG, sizeof(VBGLBIGREQ))
# define VBOXGUEST_IOCTL_CODE_FAST(Function)    _IO(  'V', (Function) | VBOXGUEST_IOCTL_FLAG)
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)       (Code)

#elif defined(RT_OS_LINUX)
  /* No automatic buffering, size limited to 16KB. */
# include <linux/ioctl.h>
# define VBOXGUEST_IOCTL_CODE(Function, Size)   _IOC(_IOC_READ|_IOC_WRITE, 'V', (Function) | VBOXGUEST_IOCTL_FLAG, (Size))
# define VBOXGUEST_IOCTL_CODE_FAST(Function)    _IO(  'V', (Function) | VBOXGUEST_IOCTL_FLAG)
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)       VBOXGUEST_IOCTL_CODE(_IOC_NR((Code)), 0)

#elif defined(RT_OS_FREEBSD) /** @todo r=bird: Please do it like SUPDRVIOC to keep it as similar as possible. */
# include <sys/ioccom.h>

# define VBOXGUEST_IOCTL_CODE(Function, Size)   _IOWR('V', (Function) | VBOXGUEST_IOCTL_FLAG, VBGLBIGREQ)
# define VBOXGUEST_IOCTL_CODE_FAST(Function)    _IO(  'V', (Function) | VBOXGUEST_IOCTL_FLAG)
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)       IOCBASECMD(Code)

#else
/* PORTME */
#endif

/** IOCTL to VBoxGuest to query the VMMDev IO port region start. */
#define VBOXGUEST_IOCTL_GETVMMDEVPORT   VBOXGUEST_IOCTL_CODE(1, sizeof(VBoxGuestPortInfo))

#pragma pack(4)
typedef struct _VBoxGuestPortInfo
{
    uint32_t portAddress;
    VMMDevMemory *pVMMDevMemory;
} VBoxGuestPortInfo;

/** IOCTL to VBoxGuest to wait for a VMMDev host notification */
#define VBOXGUEST_IOCTL_WAITEVENT       VBOXGUEST_IOCTL_CODE(2, sizeof(VBoxGuestWaitEventInfo))

/** IOCTL to VBoxGuest to interrupt (cancel) any pending WAITEVENTs and return.
 * Handled inside the guest additions and not seen by the host at all.
 * @see VBOXGUEST_IOCTL_WAITEVENT */
#define VBOXGUEST_IOCTL_CANCEL_ALL_WAITEVENTS       VBOXGUEST_IOCTL_CODE(5, 0)

/** @name Result codes for VBoxGuestWaitEventInfo::u32Result
 * @{
 */
/** Successful completion, an event occured. */
#define VBOXGUEST_WAITEVENT_OK          (0)
/** Successful completion, timed out. */
#define VBOXGUEST_WAITEVENT_TIMEOUT     (1)
/** Wait was interrupted. */
#define VBOXGUEST_WAITEVENT_INTERRUPTED (2)
/** An error occured while processing the request. */
#define VBOXGUEST_WAITEVENT_ERROR       (3)
/** @} */

/** Input and output buffers layout of the IOCTL_VBOXGUEST_WAITEVENT */
typedef struct _VBoxGuestWaitEventInfo
{
    /** timeout in milliseconds */
    uint32_t u32TimeoutIn;
    /** events to wait for */
    uint32_t u32EventMaskIn;
    /** result code */
    uint32_t u32Result;
    /** events occured */
    uint32_t u32EventFlagsOut;
} VBoxGuestWaitEventInfo;

/** IOCTL to VBoxGuest to perform a VMM request
 * @remark  The data buffer for this IOCtl has an variable size, keep this in mind
 *          on systems where this matters. */
#define VBOXGUEST_IOCTL_VMMREQUEST(Size)            VBOXGUEST_IOCTL_CODE(3, (Size))

/** Input and output buffer layout of the IOCTL_VBOXGUEST_CTL_FILTER_MASK. */
typedef struct _VBoxGuestFilterMaskInfo
{
    uint32_t u32OrMask;
    uint32_t u32NotMask;
} VBoxGuestFilterMaskInfo;
#pragma pack()

/** IOCTL to VBoxGuest to control event filter mask. */
#define VBOXGUEST_IOCTL_CTL_FILTER_MASK             VBOXGUEST_IOCTL_CODE(4, sizeof(VBoxGuestFilterMaskInfo))

/** IOCTL to VBoxGuest to check memory ballooning. */
#define VBOXGUEST_IOCTL_CTL_CHECK_BALLOON_MASK      VBOXGUEST_IOCTL_CODE(7, 100)

/** IOCTL to VBoxGuest to perform backdoor logging. */
#define VBOXGUEST_IOCTL_LOG(Size)                   VBOXGUEST_IOCTL_CODE(6, (Size))


#ifdef VBOX_WITH_HGCM
/* These structures are shared between the driver and other binaries,
 * therefore packing must be defined explicitely.
 */
# pragma pack(1)
typedef struct _VBoxGuestHGCMConnectInfo
{
    uint32_t result;          /**< OUT */
    HGCMServiceLocation Loc;  /**< IN */
    uint32_t u32ClientID;     /**< OUT */
} VBoxGuestHGCMConnectInfo;

typedef struct _VBoxGuestHGCMDisconnectInfo
{
    uint32_t result;          /**< OUT */
    uint32_t u32ClientID;     /**< IN */
} VBoxGuestHGCMDisconnectInfo;

typedef struct _VBoxGuestHGCMCallInfo
{
    uint32_t result;          /**< OUT Host HGCM return code.*/
    uint32_t u32ClientID;     /**< IN  The id of the caller. */
    uint32_t u32Function;     /**< IN  Function number. */
    uint32_t cParms;          /**< IN  How many parms. */
    /* Parameters follow in form HGCMFunctionParameter aParms[cParms] */
} VBoxGuestHGCMCallInfo;

typedef struct _VBoxGuestHGCMCallInfoTimed
{
    uint32_t u32Timeout;         /**< IN  How long to wait for completion before cancelling the call */
    uint32_t fInterruptible;     /**< IN  Is this request interruptible? */
    VBoxGuestHGCMCallInfo info;  /**< IN/OUT The rest of the call information.  Placed after the timeout
                                  * so that the parameters follow as they would for a normal call. */
    /* Parameters follow in form HGCMFunctionParameter aParms[cParms] */
} VBoxGuestHGCMCallInfoTimed;
# pragma pack()

# define VBOXGUEST_IOCTL_HGCM_CONNECT             VBOXGUEST_IOCTL_CODE(16, sizeof(VBoxGuestHGCMConnectInfo))
# define VBOXGUEST_IOCTL_HGCM_DISCONNECT          VBOXGUEST_IOCTL_CODE(17, sizeof(VBoxGuestHGCMDisconnectInfo))
/*@todo, r=Leonid 
 *This change is made to allow guest driver to distinguish between 32 and 64 bit calls on 64bit vista.
 *FIXME: "| VBOXGUEST_IOCTL_FLAG" should be moved into IOCTL_CODE define,
 *but I'm more than sure it will require more changes in vboxguest.cpp
 *Same goes for RT_OS_WINDOWS which is to make sure the change doesn't break something on other platforms.
 */
# ifdef RT_OS_WINDOWS
#  define VBOXGUEST_IOCTL_HGCM_CALL(Size)          VBOXGUEST_IOCTL_CODE(18 | VBOXGUEST_IOCTL_FLAG, (Size))
# else
#  define VBOXGUEST_IOCTL_HGCM_CALL(Size)          VBOXGUEST_IOCTL_CODE(18, (Size))
# endif
# define VBOXGUEST_IOCTL_HGCM_CALL_TIMED(Size)    VBOXGUEST_IOCTL_CODE(20, (Size))
# define VBOXGUEST_IOCTL_CLIPBOARD_CONNECT        VBOXGUEST_IOCTL_CODE(19, sizeof(uint32_t))

# ifdef VBOX_WITH_64_BITS_GUESTS
#  ifndef VBOX_HGCM_HOST_CODE
#     define  VBOXGUEST_IOCTL_HGCM_CALL_32(Size)   VBOXGUEST_IOCTL_CODE(18, (Size)) 
#  endif /* !VBOX_HGCM_HOST_CODE */
# endif /* VBOX_WITH_64_BITS_GUESTS */

# define VBOXGUEST_HGCM_CALL_PARMS(a)       ((HGCMFunctionParameter *)((uint8_t *)(a) + sizeof (VBoxGuestHGCMCallInfo)))
# define VBOXGUEST_HGCM_CALL_PARMS32(a)     ((HGCMFunctionParameter32 *)((uint8_t *)(a) + sizeof (VBoxGuestHGCMCallInfo)))

#endif /* VBOX_WITH_HGCM */

/*
 * Credentials request flags and structure
 */

#define VMMDEV_CREDENTIALS_STRLEN           128

/** query from host whether credentials are present */
#define VMMDEV_CREDENTIALS_QUERYPRESENCE     RT_BIT(1)
/** read credentials from host (can be combined with clear) */
#define VMMDEV_CREDENTIALS_READ              RT_BIT(2)
/** clear credentials on host (can be combined with read) */
#define VMMDEV_CREDENTIALS_CLEAR             RT_BIT(3)
/** read credentials for judgement in the guest */
#define VMMDEV_CREDENTIALS_READJUDGE         RT_BIT(8)
/** clear credentials for judegement on the host */
#define VMMDEV_CREDENTIALS_CLEARJUDGE        RT_BIT(9)
/** report credentials acceptance by guest */
#define VMMDEV_CREDENTIALS_JUDGE_OK          RT_BIT(10)
/** report credentials denial by guest */
#define VMMDEV_CREDENTIALS_JUDGE_DENY        RT_BIT(11)
/** report that no judgement could be made by guest */
#define VMMDEV_CREDENTIALS_JUDGE_NOJUDGEMENT RT_BIT(12)

/** flag telling the guest that credentials are present */
#define VMMDEV_CREDENTIALS_PRESENT           RT_BIT(16)
/** flag telling guest that local logons should be prohibited */
#define VMMDEV_CREDENTIALS_NOLOCALLOGON      RT_BIT(17)

/** credentials request structure */
#pragma pack(4)
typedef struct _VMMDevCredentials
{
    /* request header */
    VMMDevRequestHeader header;
    /* request flags (in/out) */
    uint32_t u32Flags;
    /* user name (UTF-8) (out) */
    char szUserName[VMMDEV_CREDENTIALS_STRLEN];
    /* password (UTF-8) (out) */
    char szPassword[VMMDEV_CREDENTIALS_STRLEN];
    /* domain name (UTF-8) (out) */
    char szDomain[VMMDEV_CREDENTIALS_STRLEN];
} VMMDevCredentials;
#pragma pack()

/** inline helper to determine the request size for the given operation */
DECLINLINE(size_t) vmmdevGetRequestSize(VMMDevRequestType requestType)
{
    switch (requestType)
    {
        case VMMDevReq_GetMouseStatus:
        case VMMDevReq_SetMouseStatus:
            return sizeof(VMMDevReqMouseStatus);
        case VMMDevReq_SetPointerShape:
            return sizeof(VMMDevReqMousePointer);
        case VMMDevReq_GetHostVersion:
            return sizeof(VMMDevReqHostVersion);
        case VMMDevReq_Idle:
            return sizeof(VMMDevReqIdle);
        case VMMDevReq_GetHostTime:
            return sizeof(VMMDevReqHostTime);
        case VMMDevReq_GetHypervisorInfo:
        case VMMDevReq_SetHypervisorInfo:
            return sizeof(VMMDevReqHypervisorInfo);
        case VMMDevReq_SetPowerStatus:
            return sizeof(VMMDevPowerStateRequest);
        case VMMDevReq_AcknowledgeEvents:
            return sizeof(VMMDevEvents);
        case VMMDevReq_ReportGuestInfo:
            return sizeof(VMMDevReportGuestInfo);
        case VMMDevReq_GetDisplayChangeRequest:
            return sizeof(VMMDevDisplayChangeRequest);
        case VMMDevReq_GetDisplayChangeRequest2:
            return sizeof(VMMDevDisplayChangeRequest2);
        case VMMDevReq_VideoModeSupported:
            return sizeof(VMMDevVideoModeSupportedRequest);
        case VMMDevReq_GetHeightReduction:
            return sizeof(VMMDevGetHeightReductionRequest);
        case VMMDevReq_ReportGuestCapabilities:
            return sizeof(VMMDevReqGuestCapabilities);
        case VMMDevReq_SetGuestCapabilities:
            return sizeof(VMMDevReqGuestCapabilities2);
#ifdef VBOX_WITH_HGCM
        case VMMDevReq_HGCMConnect:
            return sizeof(VMMDevHGCMConnect);
        case VMMDevReq_HGCMDisconnect:
            return sizeof(VMMDevHGCMDisconnect);
#ifdef VBOX_WITH_64_BITS_GUESTS
        case VMMDevReq_HGCMCall32:
            return sizeof(VMMDevHGCMCall);
        case VMMDevReq_HGCMCall64:
            return sizeof(VMMDevHGCMCall);
#else
        case VMMDevReq_HGCMCall:
            return sizeof(VMMDevHGCMCall);
#endif /* VBOX_WITH_64_BITS_GUESTS */
        case VMMDevReq_HGCMCancel:
            return sizeof(VMMDevHGCMCancel);
#endif /* VBOX_WITH_HGCM */
        case VMMDevReq_VideoAccelEnable:
            return sizeof(VMMDevVideoAccelEnable);
        case VMMDevReq_VideoAccelFlush:
            return sizeof(VMMDevVideoAccelFlush);
        case VMMDevReq_VideoSetVisibleRegion:
            return sizeof(VMMDevVideoSetVisibleRegion);
        case VMMDevReq_GetSeamlessChangeRequest:
            return sizeof(VMMDevSeamlessChangeRequest);
        case VMMDevReq_QueryCredentials:
            return sizeof(VMMDevCredentials);
        case VMMDevReq_ReportGuestStats:
            return sizeof(VMMDevReportGuestStats);
        case VMMDevReq_GetMemBalloonChangeRequest:
            return sizeof(VMMDevGetMemBalloonChangeRequest);
        case VMMDevReq_GetStatisticsChangeRequest:
            return sizeof(VMMDevGetStatisticsChangeRequest);
        case VMMDevReq_ChangeMemBalloon:
            return sizeof(VMMDevChangeMemBalloon);
        case VMMDevReq_GetVRDPChangeRequest:
            return sizeof(VMMDevVRDPChangeRequest);
        case VMMDevReq_LogString:
            return sizeof(VMMDevReqLogString);
        default:
            return 0;
    }
}

/**
 * Initializes a request structure.
 *
 */
DECLINLINE(int) vmmdevInitRequest(VMMDevRequestHeader *req, VMMDevRequestType type)
{
    uint32_t requestSize;
    if (!req)
        return VERR_INVALID_PARAMETER;
    requestSize = (uint32_t)vmmdevGetRequestSize(type);
    if (!requestSize)
        return VERR_INVALID_PARAMETER;
    req->size        = requestSize;
    req->version     = VMMDEV_REQUEST_HEADER_VERSION;
    req->requestType = type;
    req->rc          = VERR_GENERAL_FAILURE;
    req->reserved1   = 0;
    req->reserved2   = 0;
    return VINF_SUCCESS;
}


#ifdef RT_OS_OS2

/**
 * The data buffer layout for the IDC entry point (AttachDD).
 *
 * @remark  This is defined in multiple 16-bit headers / sources.
 *          Some places it's called VBGOS2IDC to short things a bit.
 */
typedef struct VBOXGUESTOS2IDCCONNECT
{
    /** VMMDEV_VERSION. */
    uint32_t u32Version;
    /** Opaque session handle. */
    uint32_t u32Session;

    /**
     * The 32-bit service entry point.
     *
     * @returns VBox status code.
     * @param   u32Session          The above session handle.
     * @param   iFunction           The requested function.
     * @param   pvData              The input/output data buffer. The caller ensures that this
     *                              cannot be swapped out, or that it's acceptable to take a
     *                              page in fault in the current context. If the request doesn't
     *                              take input or produces output, apssing NULL is okay.
     * @param   cbData              The size of the data buffer.
     * @param   pcbDataReturned     Where to store the amount of data that's returned.
     *                              This can be NULL if pvData is NULL.
     */
    DECLCALLBACKMEMBER(int, pfnServiceEP)(uint32_t u32Session, unsigned iFunction, void *pvData, size_t cbData, size_t *pcbDataReturned);

    /** The 16-bit service entry point for C code (cdecl).
     *
     * It's the same as the 32-bit entry point, but the types has
     * changed to 16-bit equivalents.
     *
     * @code
     * int far cdecl
     * VBoxGuestOs2IDCService16(uint32_t u32Session, uint16_t iFunction,
     *                          void far *fpvData, uint16_t cbData, uint16_t far *pcbDataReturned);
     * @endcode
     */
    RTFAR16 fpfnServiceEP;

    /** The 16-bit service entry point for Assembly code (register).
     *
     * This is just a wrapper around fpfnServiceEP to simplify calls
     * from 16-bit assembly code.
     *
     * @returns (e)ax: VBox status code; cx: The amount of data returned.
     *
     * @param   u32Session          eax   - The above session handle.
     * @param   iFunction           dl    - The requested function.
     * @param   pvData              es:bx - The input/output data buffer.
     * @param   cbData              cx    - The size of the data buffer.
     */
    RTFAR16 fpfnServiceAsmEP;
} VBOXGUESTOS2IDCCONNECT;
/** Pointer to VBOXGUESTOS2IDCCONNECT buffer. */
typedef VBOXGUESTOS2IDCCONNECT *PVBOXGUESTOS2IDCCONNECT;

/** OS/2 specific: IDC client disconnect request.
 *
 * This takes no input and it doesn't return anything. Obviously this
 * is only recognized if it arrives thru the IDC service EP.
 */
#define VBOXGUEST_IOCTL_OS2_IDC_DISCONNECT  VBOXGUEST_IOCTL_CODE(48, sizeof(uint32_t))

#endif /* RT_OS_OS2 */

/** @} */
#endif /* !defined(IN_RC) && !defined(IN_RING0_AGNOSTIC) && !defined(IPRT_NO_CRT) */


#ifdef IN_RING3

/** @def VBGLR3DECL
 * Ring 3 VBGL declaration.
 * @param   type    The return type of the function declaration.
 */
#define VBGLR3DECL(type) type VBOXCALL

/* General-purpose functions */

__BEGIN_DECLS
VBGLR3DECL(int)     VbglR3Init(void);
VBGLR3DECL(void)    VbglR3Term(void);
# ifdef ___iprt_time_h
VBGLR3DECL(int)     VbglR3GetHostTime(PRTTIMESPEC pTime);
# endif
VBGLR3DECL(int)     VbglR3InterruptEventWaits(void);
VBGLR3DECL(int)     VbglR3WriteLog(const char *pch, size_t cb);
VBGLR3DECL(int)     VbglR3CtlFilterMask(uint32_t fOr, uint32_t fNot);
VBGLR3DECL(int)     VbglR3Daemonize(bool fNoChDir, bool fNoClose, char const *pszPidfile);
VBGLR3DECL(int)     VbglR3SetGuestCaps(uint32_t fOr, uint32_t fNot);

/** @name Shared clipboard
 * @{ */
VBGLR3DECL(int)     VbglR3ClipboardConnect(uint32_t *pu32ClientId);
VBGLR3DECL(int)     VbglR3ClipboardDisconnect(uint32_t u32ClientId);
VBGLR3DECL(int)     VbglR3ClipboardGetHostMsg(uint32_t u32ClientId, uint32_t *pMsg, uint32_t *pfFormats);
VBGLR3DECL(int)     VbglR3ClipboardReadData(uint32_t u32ClientId, uint32_t fFormat, void *pv, uint32_t cb, uint32_t *pcb);
VBGLR3DECL(int)     VbglR3ClipboardReportFormats(uint32_t u32ClientId, uint32_t fFormats);
VBGLR3DECL(int)     VbglR3ClipboardWriteData(uint32_t u32ClientId, uint32_t fFormat, void *pv, uint32_t cb);
/** @} */

/** @name Seamless mode
 * @{ */
VBGLR3DECL(int)     VbglR3SeamlessSetCap(bool fState);
VBGLR3DECL(int)     VbglR3SeamlessWaitEvent(VMMDevSeamlessMode *pMode);
VBGLR3DECL(int)     VbglR3SeamlessSendRects(uint32_t cRects, PRTRECT pRects);
/** @}  */

/** @name Mouse
 * @{ */
VBGLR3DECL(int)     VbglR3GetMouseStatus(uint32_t *pfFeatures, uint32_t *px, uint32_t *py);
VBGLR3DECL(int)     VbglR3SetMouseStatus(uint32_t fFeatures);
/** @}  */

/** @name Video
 * @{ */
VBGLR3DECL(int)     VbglR3VideoAccelEnable(bool fEnable);
VBGLR3DECL(int)     VbglR3VideoAccelFlush(void);
VBGLR3DECL(int)     VbglR3SetPointerShape(uint32_t fFlags, uint32_t xHot, uint32_t yHot, uint32_t cx, uint32_t cy, const void *pvImg, size_t cbImg);
VBGLR3DECL(int)     VbglR3SetPointerShapeReq(VMMDevReqMousePointer *pReq);
/** @}  */

/** @name Display
 * @{ */
VBGLR3DECL(int)     VbglR3GetLastDisplayChangeRequest(uint32_t *pcx, uint32_t *pcy, uint32_t *pcBits, uint32_t *piDisplay);
VBGLR3DECL(int)     VbglR3DisplayChangeWaitEvent(uint32_t *pcx, uint32_t *pcy, uint32_t *pcBits, uint32_t *piDisplay);
VBGLR3DECL(bool)    VbglR3HostLikesVideoMode(uint32_t cx, uint32_t cy, uint32_t cBits);
VBGLR3DECL(int)     VbglR3SaveVideoMode(const char *pszName, uint32_t cx, uint32_t cy, uint32_t cBits);
VBGLR3DECL(int)     VbglR3RetrieveVideoMode(const char *pszName, uint32_t *pcx, uint32_t *pcy, uint32_t *pcBits);
/** @}  */

#ifdef VBOX_WITH_GUEST_PROPS
/** @name Guest properties
 * @{ */
typedef struct VBGLR3GUESTPROPENUM VBGLR3GUESTPROPENUM, *PVBGLR3GUESTPROPENUM;
VBGLR3DECL(int)     VbglR3GuestPropConnect(uint32_t *pu32ClientId);
VBGLR3DECL(int)     VbglR3GuestPropDisconnect(uint32_t u32ClientId);
VBGLR3DECL(int)     VbglR3GuestPropWrite(uint32_t u32ClientId, const char *pszName, const char *pszValue, const char *pszFlags);
VBGLR3DECL(int)     VbglR3GuestPropWriteValue(uint32_t u32ClientId, const char *pszName, const char *pszValue);
VBGLR3DECL(int)     VbglR3GuestPropWriteValueV(uint32_t u32ClientId, const char *pszName, const char *pszValueFormat, va_list va);
VBGLR3DECL(int)     VbglR3GuestPropWriteValueF(uint32_t u32ClientId, const char *pszName, const char *pszValueFormat, ...);
VBGLR3DECL(int)     VbglR3GuestPropRead(uint32_t u32ClientId, const char *pszName, void *pvBuf, uint32_t cbBuf, char **ppszValue, uint64_t *pu64Timestamp, char **ppszFlags, uint32_t *pcbBufActual);
VBGLR3DECL(int)     VbglR3GuestPropReadValue(uint32_t ClientId, const char *pszName, char *pszValue, uint32_t cchValue, uint32_t *pcchValueActual);
VBGLR3DECL(int)     VbglR3GuestPropReadValueAlloc(uint32_t u32ClientId, const char *pszName, char **ppszValue);
VBGLR3DECL(void)    VbglR3GuestPropReadValueFree(char *pszValue);
VBGLR3DECL(int)     VbglR3GuestPropEnumRaw(uint32_t u32ClientId, const char *paszPatterns, char *pcBuf, uint32_t cbBuf, uint32_t *pcbBufActual);
VBGLR3DECL(int)     VbglR3GuestPropEnum(uint32_t u32ClientId, char const * const *ppaszPatterns, uint32_t cPatterns, PVBGLR3GUESTPROPENUM *ppHandle,
                                        char const **ppszName, char const **ppszValue, uint64_t *pu64Timestamp, char const **ppszFlags);
VBGLR3DECL(int)     VbglR3GuestPropEnumNext(PVBGLR3GUESTPROPENUM pHandle, char const **ppszName, char const **ppszValue, uint64_t *pu64Timestamp,
                                            char const **ppszFlags);
VBGLR3DECL(void)    VbglR3GuestPropEnumFree(PVBGLR3GUESTPROPENUM pHandle);
VBGLR3DECL(int)     VbglR3GuestPropDelSet(uint32_t u32ClientId, char const * const *papszPatterns, uint32_t cPatterns);
VBGLR3DECL(int)     VbglR3GuestPropWait(uint32_t u32ClientId, const char *pszPatterns, void *pvBuf, uint32_t cbBuf, uint64_t u64Timestamp, uint32_t u32Timeout, char ** ppszName, char **ppszValue, uint64_t *pu64Timestamp, char **ppszFlags, uint32_t *pcbBufActual);
/** @}  */
#endif /* VBOX_WITH_GUEST_PROPS defined */


__END_DECLS

#endif /* IN_RING3 */

#endif
