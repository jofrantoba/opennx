/////////////////////////////////////////////////////////////////////////////
// Name:        macuninstallapp.cpp
// Purpose:     
// Author:      Fritz Elfert
// Modified by: 
// Created:     Fri 11 Sep 2009 01:17:42 PM CEST
// RCS-ID:      
// Copyright:   (C) 2009 by Fritz Elfert
// Licence:     
/////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/stdpaths.h>
#include <wx/apptrait.h>
#include <wx/filename.h>
#include <wx/cmdline.h>
#include <wx/xml/xml.h>
#include <wx/arrstr.h>
#include <wx/dir.h>
#include <Security/Authorization.h>
#include <Security/AuthorizationTags.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fstream>

#include "macuninstallapp.h"

static unsigned long failed_files;
static unsigned long failed_dirs;

class RmRfTraverser : public wxDirTraverser
{
    public:
        RmRfTraverser() { }

        ~RmRfTraverser()
        {
            int n = m_aFiles.GetCount() - 1;
            wxString fn;
            while (n >= 0) {
                fn = m_aFiles[n--];
                if (::wxRemoveFile(fn))
                    ::wxLogMessage(_("Deleted file: %s"), fn.c_str());
                else {
                    failed_files++;
                    ::wxLogWarning(_("Could not delete file %s"), fn.c_str());
                }
            }
            n = m_aDirs.GetCount() - 1;
            while (n >= 0) {
                fn = m_aDirs[n--];
                if (::wxRmdir(fn))
                    ::wxLogMessage(_("Deleted diretory %s"), fn.c_str());
                else {
                    failed_dirs++;
                    ::wxLogWarning(_("Could not delete directory %s"), fn.c_str());
                }
            }
        }

        virtual wxDirTraverseResult OnFile(const wxString& filename)
        {
            m_aFiles.Add(filename);
            return wxDIR_CONTINUE;
        }

        virtual wxDirTraverseResult OnDir(const wxString& dirpath)
        {
            m_aDirs.Add(dirpath);
            return wxDIR_CONTINUE;
        }

    private:
        wxArrayString m_aDirs;
        wxArrayString m_aFiles;
};

IMPLEMENT_APP(MacUninstallApp)
IMPLEMENT_CLASS(MacUninstallApp, wxApp)

BEGIN_EVENT_TABLE(MacUninstallApp, wxApp)
END_EVENT_TABLE()

MacUninstallApp::MacUninstallApp()
{
    failed_files = failed_dirs = 0;
    m_bBatchMode = false;
    m_bCancelled = false;
    m_sSelfPath = wxFileName(
            GetTraits()->GetStandardPaths().GetExecutablePath()).GetFullPath();
    m_nodelete.insert(wxT("."));
    m_nodelete.insert(wxT("./Applications"));
    m_nodelete.insert(wxT("./Library"));
}

void MacUninstallApp::OnInitCmdLine(wxCmdLineParser& parser)
{
    // Init standard options (--help, --verbose);
    wxApp::OnInitCmdLine(parser);
    parser.AddSwitch(wxEmptyString, wxT("batch"),
            _("Uninstall without asking the user (needs admin rights)."));
}

bool MacUninstallApp::OnCmdLineParsed(wxCmdLineParser& parser)
{
    m_bBatchMode = parser.Found(wxT("batch"));
    return true;
}

/*
 * Initialisation for MacUninstallApp
 */

bool MacUninstallApp::OnInit()
{
    wxFileName fn(m_sSelfPath);
    fn.RemoveLastDir();
    fn.AppendDir(wxT("share"));
    fn.AppendDir(wxT("locale"));
    m_cLocale.AddCatalogLookupPathPrefix(fn.GetPath());
    m_cLocale.Init();
    m_cLocale.AddCatalog(wxT("opennx"));

    // Call to base class needed for initializing command line processing
    if (!wxApp::OnInit())
        return false;

    wxInitAllImageHandlers();
    wxBitmap::InitStandardHandlers();
    m_sLogName << wxT("/tmp/uninstall-") << wxT("OpenNX") << wxT(".log");
    if (m_bBatchMode) {
        if (0 != geteuid()) {
            ::wxMessageBox(_("Batch uninstall needs to be started as root."),
                    _("Uninstall OpenNX"), wxOK|wxICON_ERROR);
            while (Pending())
                Dispatch();
            return false;
        }
        bool ok = DoUninstall(wxT("OpenNX"));
        ::wxLogMessage(_("Uninstall finished at %s"), wxDateTime::Now().Format().c_str());
        printf("%d %lu %lu\n", ok ? 0 : 1, failed_files, failed_dirs);
    } else {
        int r = ::wxMessageBox(
                _("This operation can not be undone!\nDo you really want to uninstall OpenNX?"),
                _("Uninstall OpenNX"), wxYES_NO|wxNO_DEFAULT|wxICON_EXCLAMATION);
        if (wxYES == r) {
            if (!TestReceipt(wxT("OpenNX"))) {
                while (Pending())
                    Dispatch();
                return false;
            }
            if (ElevatedUninstall(wxT("OpenNX"))) {
                if (!m_bCancelled) {
                    if (0 == (failed_files + failed_dirs)) {
                        ::wxMessageBox(_("OpenNX has been removed successfully."),
                                _("Uninstallation complete"), wxOK|wxICON_INFORMATION);
                    } else {
                        ::wxMessageBox(
                                wxString::Format(
                                    _("OpenNX could not be removed completely.\nSome files or directories could not be deleted.\nPlease investigate the log file\n%s\n for more information."),
                                    m_sLogName.c_str()),
                                _("Uninstallation incomplete"), wxOK|wxICON_EXCLAMATION);
                    }
                }
            } else
                ::wxMessageBox(
                        wxString::Format(
                            _("Uninstallation has failed.\nThe reason should have been logged in the file\n%s"),
                            m_sLogName.c_str()),
                        _("Uninstallation failed"), wxOK|wxICON_ERROR);
        }
    }
    while (Pending())
        Dispatch();
    return false;
}

int MacUninstallApp::OnExit()
{
    return wxApp::OnExit();
}

wxString MacUninstallApp::GetInstalledPath(const wxString &rcpt)
{
    wxXmlDocument doc(rcpt);
    if (doc.IsOk()) {
        if (doc.GetRoot()->GetName() != wxT("plist")) {
            ::wxLogError(_("Not an XML plist: %s"), rcpt.c_str());
            return wxEmptyString;
        }
        wxXmlNode *child = doc.GetRoot()->GetChildren();
        if (child->GetName() != wxT("dict")) {
            ::wxLogError(
                    _("Invalid plist (missing toplevel <dict> in %s"),
                    rcpt.c_str());
            return wxEmptyString;
        }
        child = child->GetChildren();
        bool needkey = true;
        bool found = false;
        while (child) {
            if (needkey) {
                if (child->GetName() != wxT("key")) {
                    ::wxLogError(
                            _("Invalid plist (expected a key) in %s"),
                            rcpt.c_str());
                    return wxEmptyString;
                }
                if (child->GetNodeContent() == wxT("IFPkgRelocatedPath"))
                    found = true;
            } else {
                if (found) {
                    if (child->GetName() != wxT("string")) {
                        ::wxLogError(
                                _("Invalid plist (expected a string value) in %s"),
                                rcpt.c_str());
                        return wxEmptyString;
                    }
                    return child->GetNodeContent();
                }
            }
            needkey = (!needkey);
            child = child->GetNext();
        }
    } else
        ::wxLogError(_("Could not read package receipt %s"), rcpt.c_str());
    return wxEmptyString;
}

bool MacUninstallApp::FetchBOM(const wxString &bom,
        wxArrayString &dirs, wxArrayString &files)
{
    if (!wxFileName::FileExists(bom)) {
        ::wxLogError(
                _("Missing BOM (Bill Of Materials) '%s'. Already unistalled?"), bom.c_str());
        return false;
    }
    wxString cmd(wxT("lsbom -fbcl -p f "));
    cmd << bom;
    wxArrayString err;
    if (0 != ::wxExecute(cmd, files, err)) {
        ::wxLogError(
                _("Could not list BOM (Bill Of Materials) '%s'. Already unistalled?"), bom.c_str());
        return false;
    }
    if (0 != err.GetCount() != 0) {
        ::wxLogError(
                _("Invalid BOM (Bill Of Materials) '%s'. Already unistalled?"), bom.c_str());
        return false;
    }
    cmd = wxT("lsbom -d -p f ");
    cmd << bom;
    err.Empty();
    if (0 != ::wxExecute(cmd, dirs, err)) {
        ::wxLogError(
                _("Could not list BOM (Bill Of Materials) '%s'. Already unistalled?"), bom.c_str());
        return false;
    }
    if (0 != err.GetCount() != 0) {
        ::wxLogError(
                _("Invalid BOM (Bill Of Materials) '%s'. Already unistalled?"), bom.c_str());
        return false;
    }
    return true;
}

bool MacUninstallApp::TestReceipt(const wxString &pkg)
{
    wxString rpath = wxT("/Library/Receipts/");
    rpath.Append(pkg).Append(wxT(".pkg"));
    if (!wxFileName::DirExists(rpath)) {
        ::wxLogWarning(
                _("The package receipt does not exist. Already unistalled?"));
        return false;
    }
    wxString proot = GetInstalledPath(rpath + wxT("/Contents/Info.plist"));
    if (proot.IsEmpty())
        return false;
    if (!wxFileName::DirExists(proot)) {
        ::wxLogWarning(
                _("The package install path does not exist. Already unistalled?"));
        return false;
    }
    wxArrayString d;
    wxArrayString f;
    if (!FetchBOM(rpath + wxT("/Contents/Archive.bom"), d, f))
        return false;
    return true;
}

bool MacUninstallApp::DoUninstall(const wxString &pkg)
{
    std::ofstream *log = new std::ofstream();
    log->open(m_sLogName.mb_str());
    delete wxLog::SetActiveTarget(new wxLogStream(log));
    ::wxLogMessage(wxT("Uninstall started at %s"), wxDateTime::Now().Format().c_str());
    wxString rpath = wxT("/Library/Receipts/");
    rpath.Append(pkg).Append(wxT(".pkg"));
    wxString proot = GetInstalledPath(rpath + wxT("/Contents/Info.plist"));
    if (proot.IsEmpty())
        return false;
    wxArrayString d;
    wxArrayString f;
    if (!FetchBOM(rpath + wxT("/Contents/Archive.bom"), d, f))
        return false;
    size_t i;
    ::wxLogMessage(wxT("Deleting package content"));
    for (i = 0; i < f.GetCount(); i++) {
        if (m_nodelete.find(f[i]) != m_nodelete.end()) {
            f.RemoveAt(i--);
            continue;
        }
        wxFileName fn(proot + f[i]);
        if (fn.Normalize(wxPATH_NORM_DOTS|wxPATH_NORM_ABSOLUTE)) {
            wxString name = fn.GetFullPath();
            if (::wxRemoveFile(name) || (!fn.FileExists())) {
                f.RemoveAt(i--);
                ::wxLogMessage(_("Deleted file: %s"), name.c_str());
            } else {
                failed_files++;
                ::wxLogWarning(_("Could not delete file %s"), name.c_str());
            }
        }
    }
    size_t lcd;
    do {
        lcd = d.GetCount();
        for (i = 0; i < d.GetCount(); i++) {
            if (m_nodelete.find(d[i]) != m_nodelete.end()) {
                d.RemoveAt(i--);
                continue;
            }
            wxFileName fn(proot + d[i]);
            if (fn.Normalize(wxPATH_NORM_DOTS|wxPATH_NORM_ABSOLUTE)) {
                wxString name = fn.GetFullPath();
                if (::wxRmdir(name) || (!fn.DirExists())) {
                    d.RemoveAt(i--);
                    ::wxLogMessage(_("Deleted directory: %s"), name.c_str());
                }
            }
        }
    } while (lcd != d.GetCount());
    if (0 < d.GetCount()) {
        for (i = 0; i < d.GetCount(); i++) {
            failed_dirs++;
            ::wxLogWarning(_("Could not delete directory %s"), d[i].c_str());
        }
    }
    if (0 == (d.GetCount() + f.GetCount())) {
        // Finally delete the receipe itself
        ::wxLogMessage(wxT("Deleting receipt"));
        {
            wxDir d(rpath);
            RmRfTraverser t;
            d.Traverse(t);
        }
        if (::wxRmdir(rpath))
            ::wxLogMessage(_("Deleted directory: %s"), rpath.c_str());
        else {
            failed_dirs++;
            ::wxLogWarning(_("Could not delete directory %s"), rpath.c_str());
        }
    } else
        ::wxLogMessage(_("Receipt NOT deleted, because package files have been left."));
    return true;
}

/**
 * Execute with administrative rights.
 */
bool MacUninstallApp::ElevatedUninstall(const wxString &pkg)
{
    if (geteuid() == 0)
        return DoUninstall(pkg);

    wxString msg = wxString::Format(
            _("In order to uninstall %s, administrative rights are required.\n\n"),
            pkg.c_str());
    char *prompt = strdup(msg.utf8_str());

    OSStatus st;
    AuthorizationFlags aFlags = kAuthorizationFlagDefaults;
    AuthorizationRef aRef;
    AuthorizationItem promptItem = {
        kAuthorizationEnvironmentPrompt, strlen(prompt), prompt, 0
    };
    AuthorizationEnvironment aEnv = { 1, &promptItem };

    st = AuthorizationCreate(NULL, kAuthorizationEmptyEnvironment,
            kAuthorizationFlagDefaults, &aRef);
    if (errAuthorizationSuccess != st) {
        ::wxLogError(_("Authorization could not be created: %s"), MacAuthError(st).c_str());
        return true;
    }
    AuthorizationItem aItems = { kAuthorizationRightExecute, 0, NULL, 0 };
    AuthorizationRights aRights = { 1, &aItems };
    aFlags = kAuthorizationFlagDefaults |
        kAuthorizationFlagInteractionAllowed |
        kAuthorizationFlagPreAuthorize |
        kAuthorizationFlagExtendRights;
    st = AuthorizationCopyRights(aRef, &aRights, &aEnv, aFlags, NULL );
    bool ret = true;
    if (errAuthorizationSuccess == st) {
        char *executable = strdup(m_sSelfPath.utf8_str());
        char *args[] = { "--batch", NULL };
        FILE *pout = NULL;
        st = AuthorizationExecuteWithPrivileges(aRef,
                executable, kAuthorizationFlagDefaults, args, &pout);
        if (errAuthorizationSuccess == st) {
            int status;
            fscanf(pout, "%d %lu %lu", &status, &failed_files, &failed_dirs);
            ret = (0 == status);
        } else
            ::wxLogError(_("Could not execute with administrative rights:\n%s"), MacAuthError(st).c_str());
    } else {
        if (st) {
            m_bCancelled = (errAuthorizationCanceled == st);
            if (!m_bCancelled)
                ::wxLogError(_("Authorization failed: %s"), MacAuthError(st).c_str());
        }
    }
    AuthorizationFree(aRef, kAuthorizationFlagDefaults);
    return ret;
}

wxString MacUninstallApp::MacAuthError(long code)
{
    wxString ret;
    switch (code) {
        case errAuthorizationSuccess:
            return wxT("The operation completed successfully.");
        case errAuthorizationInvalidSet:
            return wxT("The set parameter is invalid.");
        case errAuthorizationInvalidRef:
            return wxT("The authorization parameter is invalid.");
        case errAuthorizationInvalidTag:
            return wxT("The tag parameter is invalid.");
        case errAuthorizationInvalidPointer:
            return wxT("The authorizedRights parameter is invalid.");
        case errAuthorizationDenied:
            return wxT("The Security Server denied authorization for one or more requested rights.");
        case errAuthorizationCanceled:
            return wxT("The user canceled the operation.");
        case errAuthorizationInteractionNotAllowed:
            return wxT("The Security Server denied authorization because no user interaction is allowed.");
        case errAuthorizationInternal:
            return wxT("An unrecognized internal error occurred.");
        case errAuthorizationExternalizeNotAllowed:
            return wxT("The Security Server denied externalization of the authorization reference.");
        case errAuthorizationInternalizeNotAllowed:
            return wxT("The Security Server denied internalization of the authorization reference.");
        case errAuthorizationInvalidFlags:
            return wxT("The flags parameter is invalid.");
        case errAuthorizationToolExecuteFailure:
            return wxT("The tool failed to execute.");
        case errAuthorizationToolEnvironmentError:
            return wxT("The attempt to execute the tool failed to return a success or an error code.");
    }
    return ret;
}
