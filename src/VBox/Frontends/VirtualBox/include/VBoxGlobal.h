/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * VBoxGlobal class declaration
 */

/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 */

#ifndef __VBoxGlobal_h__
#define __VBoxGlobal_h__

#include "COMDefs.h"

#include "VMGlobalSettings.h"

#include <qapplication.h>
#include <qpixmap.h>
#include <qiconset.h>
#include <qcolor.h>
#include <quuid.h>
#include <qthread.h>

#include <qptrvector.h>
#include <qvaluevector.h>
#include <qvaluelist.h>
#include <qdict.h>
#include <qintdict.h>

// Auxiliary types
////////////////////////////////////////////////////////////////////////////////

/** Simple media descriptor type. */
struct VBoxMedia
{
    enum Status { Unknown, Ok, Error, Inaccessible };

    VBoxMedia() : type (VBoxDefs::InvalidType), status (Ok) {}

    VBoxMedia (const CUnknown &d, VBoxDefs::DiskType t, Status s)
        : disk (d), type (t), status (s) {}

    CUnknown disk;
    VBoxDefs::DiskType type;
    Status status;
};

typedef QValueList <VBoxMedia> VBoxMediaList;

// VirtualBox callback events
////////////////////////////////////////////////////////////////////////////////

class VBoxMachineStateChangeEvent : public QEvent
{
public:
    VBoxMachineStateChangeEvent (const QUuid &aId, CEnums::MachineState aState)
        : QEvent ((QEvent::Type) VBoxDefs::MachineStateChangeEventType)
        , id (aId), state (aState)
        {}

    const QUuid id;
    const CEnums::MachineState state;
};

class VBoxMachineDataChangeEvent : public QEvent
{
public:
    VBoxMachineDataChangeEvent (const QUuid &aId)
        : QEvent ((QEvent::Type) VBoxDefs::MachineDataChangeEventType)
        , id (aId)
        {}

    const QUuid id;
};

class VBoxMachineRegisteredEvent : public QEvent
{
public:
    VBoxMachineRegisteredEvent (const QUuid &aId, bool aRegistered)
        : QEvent ((QEvent::Type) VBoxDefs::MachineRegisteredEventType)
        , id (aId), registered (aRegistered)
        {}

    const QUuid id;
    const bool registered;
};

class VBoxSessionStateChangeEvent : public QEvent
{
public:
    VBoxSessionStateChangeEvent (const QUuid &aId, CEnums::SessionState aState)
        : QEvent ((QEvent::Type) VBoxDefs::SessionStateChangeEventType)
        , id (aId), state (aState)
        {}

    const QUuid id;
    const CEnums::SessionState state;
};

class VBoxSnapshotEvent : public QEvent
{
public:

    enum What { Taken, Discarded, Changed };

    VBoxSnapshotEvent (const QUuid &aMachineId, const QUuid &aSnapshotId,
                       What aWhat)
        : QEvent ((QEvent::Type) VBoxDefs::SnapshotEventType)
        , what (aWhat)
        , machineId (aMachineId), snapshotId (aSnapshotId)
        {}

    const What what;

    const QUuid machineId;
    const QUuid snapshotId;
};

// VBoxGlobal
////////////////////////////////////////////////////////////////////////////////

class VBoxSelectorWnd;
class VBoxConsoleWnd;

class VBoxGlobal : public QObject
{
    Q_OBJECT

public:

    static VBoxGlobal &instance();

    bool isValid() { return valid; }

    QString versionString() { return verString; }

    CVirtualBox virtualBox() const { return vbox; }

    const VMGlobalSettings &settings() const { return gset; }
    bool setSettings (const VMGlobalSettings &gs);

    VBoxSelectorWnd &selectorWnd();
    VBoxConsoleWnd &consoleWnd();

    bool isVMConsoleProcess() const { return !vmUuid.isNull(); }
    QUuid managedVMUuid() const { return vmUuid; }

    VBoxDefs::RenderMode vmRenderMode() const { return vm_render_mode; }
    const char *vmRenderModeStr() const { return vm_render_mode_str; }

#ifdef VBOX_WITH_DEBUGGER_GUI
    bool isDebuggerEnabled() const { return dbg_enabled; }
    bool isDebuggerVisibleAtStartup() const { return dbg_visible_at_startup; }
#endif

    // VBox enum to/from string/icon/color convertors

    QStringList vmGuestOSTypeDescriptions() const;
    CGuestOSType vmGuestOSType (int index) const;
    int vmGuestOSTypeIndex (const CGuestOSType &type) const;
    QPixmap vmGuestOSTypeIcon (const QString &type) const;

    QPixmap toIcon (CEnums::MachineState s) const
    {
        QPixmap *pm = mStateIcons [s];
        AssertMsg (pm, ("Icon for VM state %d must be defined", s));
        return pm ? *pm : QPixmap();
    }

    const QColor &toColor (CEnums::MachineState s) const
    {
        static const QColor none;
        AssertMsg (vm_state_color [s], ("No color for %d", s));
        return vm_state_color [s] ? *vm_state_color [s] : none;
    }

    QString toString (CEnums::MachineState s) const
    {
        AssertMsg (!machineStates [s].isNull(), ("No text for %d", s));
        return machineStates [s];
    }

    QString toString (CEnums::SessionState s) const
    {
        AssertMsg (!sessionStates [s].isNull(), ("No text for %d", s));
        return sessionStates [s];
    }

    QString toString (CEnums::DiskControllerType t) const
    {
        AssertMsg (!diskControllerTypes [t].isNull(), ("No text for %d", t));
        return diskControllerTypes [t];
    }

    QString toString (CEnums::HardDiskType t) const
    {
        AssertMsg (!diskTypes [t].isNull(), ("No text for %d", t));
        return diskTypes [t];
    }

    QString toString (CEnums::HardDiskStorageType t) const
    {
        AssertMsg (!diskStorageTypes [t].isNull(), ("No text for %d", t));
        return diskStorageTypes [t];
    }

    QString toString (CEnums::VRDPAuthType t) const
    {
        AssertMsg (!vrdpAuthTypes [t].isNull(), ("No text for %d", t));
        return vrdpAuthTypes [t];
    }

    CEnums::VRDPAuthType toVRDPAuthType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (vrdpAuthTypes.begin(), vrdpAuthTypes.end(), s);
        AssertMsg (it != vrdpAuthTypes.end(), ("No value for {%s}", s.latin1()));
        return CEnums::VRDPAuthType (it - vrdpAuthTypes.begin());
    }

    /**
     *  Similar to toString (CEnums::HardDiskType), but returns 'Differencing'
     *  for normal hard disks that have a parent hard disk.
     */
    QString hardDiskTypeString (const CHardDisk &aHD) const
    {
        if (!aHD.GetParent().isNull())
        {
            Assert (aHD.GetType() == CEnums::NormalHardDisk);
            return tr ("Differencing", "hard disk");
        }
        return toString (aHD.GetType());
    }

    QString toString (CEnums::DiskControllerType t, LONG d) const;

    QString toString (CEnums::DeviceType t) const
    {
        AssertMsg (!deviceTypes [t].isNull(), ("No text for %d", t));
        return deviceTypes [t];
    }

    CEnums::DeviceType toDeviceType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (deviceTypes.begin(), deviceTypes.end(), s);
        AssertMsg (it != deviceTypes.end(), ("No value for {%s}", s.latin1()));
        return CEnums::DeviceType (it - deviceTypes.begin());
    }

    QStringList deviceTypeStrings() const;

    QString toString (CEnums::AudioDriverType t) const
    {
        AssertMsg (!audioDriverTypes [t].isNull(), ("No text for %d", t));
        return audioDriverTypes [t];
    }

    CEnums::AudioDriverType toAudioDriverType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (audioDriverTypes.begin(), audioDriverTypes.end(), s);
        AssertMsg (it != audioDriverTypes.end(), ("No value for {%s}", s.latin1()));
        return CEnums::AudioDriverType (it - audioDriverTypes.begin());
    }

    QString toString (CEnums::NetworkAttachmentType t) const
    {
        AssertMsg (!networkAttachmentTypes [t].isNull(), ("No text for %d", t));
        return networkAttachmentTypes [t];
    }

    CEnums::NetworkAttachmentType toNetworkAttachmentType (const QString &s) const
    {
        QStringVector::const_iterator it =
            qFind (networkAttachmentTypes.begin(), networkAttachmentTypes.end(), s);
        AssertMsg (it != networkAttachmentTypes.end(), ("No value for {%s}", s.latin1()));
        return CEnums::NetworkAttachmentType (it - networkAttachmentTypes.begin());
    }

    QString toString (CEnums::USBDeviceState aState) const
    {
        AssertMsg (!USBDeviceStates [aState].isNull(), ("No text for %d", aState));
        return USBDeviceStates [aState];
    }

    QPixmap snapshotIcon (bool online) const
    {
        return online ? mOnlineSnapshotIcon : mOfflineSnapshotIcon;
    }

    /* details generators */

    QString details (const CHardDisk &aHD, bool aPredict = false) const;

    QString details (const CUSBDevice &aDevice) const;
    QString toolTip (const CUSBDevice &aDevice) const;

    QString prepareFileNameForHTML (const QString &fn) const;

    QString detailsReport (const CMachine &m, bool isNewVM, bool withLinks) const;

    /* VirtualBox helpers */

    CSession openSession (const QUuid &id);

    bool startMachine (const QUuid &id);

    void startEnumeratingMedia();

    /**
     *  Returns a list of all currently registered media. This list is used
     *  to globally track the accessiblity state of all media on a dedicated
     *  thread. This the list is initially empty (before the first enumeration
     *  process is started using #startEnumeratingMedia()).
     */
    const VBoxMediaList &currentMediaList() const { return media_list; }

    /** Returns true if the media enumeration is in progress. */
    bool isMediaEnumerationStarted() const { return media_enum_thread != NULL; }

    void addMedia (const VBoxMedia &);
    void updateMedia (const VBoxMedia &);
    void removeMedia (VBoxDefs::DiskType, const QUuid &);

    /* various helpers */

    void languageChange();

    /* made public for internal purposes */
    void cleanup();

    /* public static stuff */

    static QIconSet iconSet (const char *aNormal,
                             const char *aDisabled = 0,
                             const char *aActive = 0);
    static QIconSet iconSetEx (const char *aNormal, const char *aSmallNormal,
                               const char *aDisabled = 0, const char *aSmallDisabled = 0,
                               const char *aActive = 0, const char *aSmallActive = 0);

    static QRect normalizeGeometry (const QRect &aRect, const QRect &aBoundRect,
                                    bool aCanResize = true);

    static void centerWidget (QWidget *aWidget, QWidget *aRelative,
                              bool aCanResize = true);

    static QChar decimalSep();
    static QString sizeRegexp();

    static Q_UINT64 parseSize (const QString &);
    static QString formatSize (Q_UINT64, int aMode = 0);

    static QString highlight (const QString &aStr, bool aToolTip = false);

signals:

    /**
     *  Emitted at the beginning of the enumeration process started
     *  by #startEnumeratingMedia().
     */
    void mediaEnumStarted();

    /**
     *  Emitted when a new media item from the list has updated
     *  its accessibility state.
     */
    void mediaEnumerated (const VBoxMedia &aMedia, int aIndex);

    /**
     *  Emitted at the end of the enumeration process started
     *  by #startEnumeratingMedia().
     *  @note #currentMediaList() will return an empty list
     *  when this signal is emitted, use the argument instead.
     */
    void mediaEnumFinished (const VBoxMediaList &aList);

    /** Emitted when a new media is added using #addMedia(). */
    void mediaAdded (const VBoxMedia &);

    /** Emitted when the media is updated using #updateMedia(). */
    void mediaUpdated (const VBoxMedia &);

    /** Emitted when the media is removed using #removeMedia(). */
    void mediaRemoved (VBoxDefs::DiskType, const QUuid &);

    /* signals emitted when the VirtualBox callback is called by the server
     * (not that currently these signals are emitted only when the application
     * is the in the VM selector mode) */

    void machineStateChanged (const VBoxMachineStateChangeEvent &e);
    void machineDataChanged (const VBoxMachineDataChangeEvent &e);
    void machineRegistered (const VBoxMachineRegisteredEvent &e);
    void sessionStateChanged (const VBoxSessionStateChangeEvent &e);
    void snapshotChanged (const VBoxSnapshotEvent &e);

protected:

    bool event (QEvent *e);

private:

    VBoxGlobal();
    ~VBoxGlobal() {}

    void init();

    bool valid;

    CVirtualBox vbox;

    VMGlobalSettings gset;

    VBoxSelectorWnd *selector_wnd;
    VBoxConsoleWnd *console_wnd;

    QUuid vmUuid;

    QThread *media_enum_thread;
    VBoxMediaList media_list;

    VBoxDefs::RenderMode vm_render_mode;
    const char * vm_render_mode_str;

#ifdef VBOX_WITH_DEBUGGER_GUI
    bool dbg_enabled;
    bool dbg_visible_at_startup;
#endif

#if defined (Q_WS_WIN32)
    DWORD dwHTMLHelpCookie;
#endif

    CVirtualBoxCallback callback;

    typedef QValueVector <QString> QStringVector;

    QString verString;

    QValueVector <CGuestOSType> vm_os_types;
    QDict <QPixmap> vm_os_type_icons;
    QPtrVector <QColor> vm_state_color;

    QIntDict <QPixmap> mStateIcons;
    QPixmap mOfflineSnapshotIcon, mOnlineSnapshotIcon;

    QStringVector machineStates;
    QStringVector sessionStates;
    QStringVector deviceTypes;
    QStringVector diskControllerTypes;
    QStringVector diskTypes;
    QStringVector diskStorageTypes;
    QStringVector vrdpAuthTypes;
    QStringVector diskControllerDevices;
    QStringVector audioDriverTypes;
    QStringVector networkAttachmentTypes;
    QStringVector USBDeviceStates;

    mutable bool detailReportTemplatesReady;

    friend VBoxGlobal &vboxGlobal();
    friend class VBoxCallback;
};

inline VBoxGlobal &vboxGlobal() { return VBoxGlobal::instance(); }

#endif /* __VBoxGlobal_h__ */

