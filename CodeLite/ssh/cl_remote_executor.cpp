#if USE_SFTP
#include "ssh/cl_remote_executor.hpp"

#include "StringUtils.h"
#include "clModuleLogger.hpp"
#include "cl_standard_paths.h"
#include "processreaderthread.h"
#include "ssh/ssh_account_info.h"

#include <thread>

clModuleLogger REMOTE_LOG;
namespace
{
bool once = true;
const wxString TERMINATOR = "CODELITE_TERMINATOR";
} // namespace

clRemoteExecutor::clRemoteExecutor()
{
    if(once) {
        wxFileName logfile{ clStandardPaths::Get().GetUserDataDir(), "remote-executor.log" };
        logfile.AppendDir("logs");
        REMOTE_LOG.Open(logfile.GetFullPath());
        once = true;
    }

    Bind(wxEVT_SSH_CHANNEL_READ_ERROR, &clRemoteExecutor::OnChannelError, this);
    Bind(wxEVT_SSH_CHANNEL_WRITE_ERROR, &clRemoteExecutor::OnChannelError, this);
    Bind(wxEVT_SSH_CHANNEL_READ_OUTPUT, &clRemoteExecutor::OnChannelStdout, this);
    Bind(wxEVT_SSH_CHANNEL_READ_STDERR, &clRemoteExecutor::OnChannelStderr, this);
    Bind(wxEVT_SSH_CHANNEL_CLOSED, &clRemoteExecutor::OnChannelClosed, this);
}

clRemoteExecutor::~clRemoteExecutor()
{
    shutdown();
    Unbind(wxEVT_SSH_CHANNEL_READ_ERROR, &clRemoteExecutor::OnChannelError, this);
    Unbind(wxEVT_SSH_CHANNEL_WRITE_ERROR, &clRemoteExecutor::OnChannelError, this);
    Unbind(wxEVT_SSH_CHANNEL_READ_OUTPUT, &clRemoteExecutor::OnChannelStdout, this);
    Unbind(wxEVT_SSH_CHANNEL_READ_STDERR, &clRemoteExecutor::OnChannelStderr, this);
    Unbind(wxEVT_SSH_CHANNEL_CLOSED, &clRemoteExecutor::OnChannelClosed, this);
}

bool clRemoteExecutor::startup(const wxString& account_name)
{
    if(m_ssh) {
        return true;
    }

    auto account = SSHAccountInfo::LoadAccount(account_name);
    if(account.GetHost().empty()) {
        return false;
    }

    /// open channel
    try {
        m_ssh.reset(new clSSH(account.GetHost(), account.GetUsername(), account.GetPassword(), account.GetPort()));
        wxString message;

        m_ssh->Connect();
        if(!m_ssh->AuthenticateServer(message)) {
            m_ssh->AcceptServerAuthentication();
        }
        m_ssh->Login();
    } catch(clException& e) {
        LOG_ERROR(REMOTE_LOG) << "Failed to open ssh channel to account:" << account.GetAccountName() << "." << e.What()
                              << endl;
        return false;
    }
    return true;
}

void clRemoteExecutor::shutdown() { m_ssh.reset(); }

clSSHChannel* clRemoteExecutor::try_execute(const clRemoteExecutor::Cmd& cmd)
{
    wxString command;
    if(!m_ssh) {
        LOG_WARNING(REMOTE_LOG) << "SSH session is not opened" << endl;
        return nullptr;
    }

    // open the channel
    clSSHChannel* channel = nullptr;
    try {
        channel = new clSSHChannel(m_ssh, clSSHChannel::kRemoteCommand, this, true);
        channel->Open();
    } catch(clException& e) {
        LOG_ERROR(REMOTE_LOG) << "failed to open channel." << e.What() << endl;
        wxDELETE(channel);
        return nullptr;
    }

    if(!cmd.env.empty()) {
        // build each env in its own "export" statement
        for(const auto& e : cmd.env) {
            command << "export " << e.first << "=" << e.second << ";";
        }
    }

    if(!cmd.wd.empty()) {
        command << "cd " << StringUtils::WrapWithDoubleQuotes(cmd.wd) << " && ";
    }

    for(const wxString& c : cmd.command) {
        command << StringUtils::WrapWithDoubleQuotes(c) << " ";
    }

    if(command.EndsWith(" ")) {
        command.RemoveLast();
    }

    LOG_DEBUG(REMOTE_LOG) << "Executing command:" << command << endl;

    // prepare the commands

    try {
        channel->Execute(command);
        LOG_DEBUG(REMOTE_LOG) << "Success" << endl;
        return channel;
    } catch(clException& e) {
        LOG_TRACE(REMOTE_LOG) << "failed to execute remote command." << command << "." << e.What() << endl;
        wxDELETE(channel);
    }

    // the channel will delete itself upon completion
    return nullptr;
}

void clRemoteExecutor::OnChannelStdout(clCommandEvent& event)
{
    m_output.append(event.GetStringRaw());
    LOG_DEBUG(REMOTE_LOG) << m_output << endl;
}

void clRemoteExecutor::OnChannelStderr(clCommandEvent& event)
{
    m_output.append(event.GetStringRaw());
    LOG_DEBUG(REMOTE_LOG) << m_output << endl;
}

void clRemoteExecutor::OnChannelClosed(clCommandEvent& event)
{
    LOG_DEBUG(REMOTE_LOG) << "remote command completed" << endl;

    clShellProcessEvent output_event{ wxEVT_SHELL_ASYNC_REMOTE_PROCESS_TERMINATED };
    output_event.SetStringRaw(m_output);
    output_event.SetExitCode(0);
    ProcessEvent(output_event);

    m_output.clear();
}

void clRemoteExecutor::OnChannelError(clCommandEvent& event)
{
    wxUnusedVar(event);
    clShellProcessEvent command_ended{ wxEVT_SHELL_ASYNC_REMOTE_PROCESS_TERMINATED };
    command_ended.SetExitCode(127);
    ProcessEvent(command_ended);
}

wxDEFINE_EVENT(wxEVT_SHELL_ASYNC_REMOTE_PROCESS_TERMINATED, clShellProcessEvent);
#endif // USE_SFTP
