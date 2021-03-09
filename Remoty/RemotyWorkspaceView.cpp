#include "RemotyWorkspaceView.h"
#include "file_logger.h"
#include "ssh_account_info.h"

RemotyWorkspaceView::RemotyWorkspaceView(wxWindow* parent)
    : RemotyWorkspaceViewBase(parent)
{
    m_tree = new clRemoteDirCtrl(this);
    GetSizer()->Add(m_tree, 1, wxEXPAND);
    GetSizer()->Fit(this);
}

RemotyWorkspaceView::~RemotyWorkspaceView() {}

void RemotyWorkspaceView::Open(const wxString& path, const wxString& accountName)
{
    auto account = SSHAccountInfo::LoadAccount(accountName);
    if(account.GetAccountName().empty()) {
        clWARNING() << "Failed to open workspace at:" << path << "for account" << accountName << endl;
        clWARNING() << "No such account exist" << endl;
    }
}
