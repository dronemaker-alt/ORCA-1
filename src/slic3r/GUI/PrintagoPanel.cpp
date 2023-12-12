#include "WebViewDialog.hpp"

#include "I18N.hpp"
#include "../Utils/Http.hpp"
#include "nlohmann/json.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <slic3r/GUI/Widgets/WebView.hpp>
#include <wx/datetime.h>
#include <wx/sizer.h>
#include <wx/url.h>

using namespace nlohmann;

namespace pt = boost::property_tree;

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT, PrintagoMessageEvent);
wxDEFINE_EVENT(PRINTAGO_COMMAND_EVENT, PrintagoCommandEvent);
wxDEFINE_EVENT(PRINTAGO_SLICING_PROCESS_COMPLETED_EVENT, SlicingProcessCompletedEvent);

#define PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL 170.0f // Minimum temperature to allow extrusion control (per StatusPanel.cpp)

PrintagoPanel::PrintagoPanel(wxWindow *parent, wxString *url) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    devManager           = Slic3r::GUI::wxGetApp().getDeviceManager();
    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);

    // Create the info panel
    m_info = new wxInfoBar(this);
    topsizer->Add(m_info, wxSizerFlags().Expand());
    // Create the webview
    m_browser = WebView::CreateWebView(this, *url);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }
    m_browser->Hide();
    SetSizer(topsizer);

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &PrintagoPanel::OnNavigationRequest, this);
    Bind(wxEVT_WEBVIEW_NAVIGATED, &PrintagoPanel::OnNavigationComplete, this);
    Bind(wxEVT_WEBVIEW_LOADED, &PrintagoPanel::OnDocumentLoaded, this);
    Bind(wxEVT_WEBVIEW_ERROR, &PrintagoPanel::OnError, this);
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &PrintagoPanel::OnNewWindow, this);
    Bind(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT, &PrintagoPanel::OnPrintagoSendWebViewMessage, this);
    Bind(PRINTAGO_COMMAND_EVENT, &PrintagoPanel::HandlePrintagoCommand, this);
    Bind(EVT_PROCESS_COMPLETED, &PrintagoPanel::OnSlicingProcessCompleted, this);
}

PrintagoPanel::~PrintagoPanel()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}

void PrintagoPanel::load_url(wxString &url)
{
    this->Show();
    this->Raise();

    m_browser->LoadURL(url);
    m_browser->SetFocus();
}

void PrintagoPanel::set_can_process_job(const bool can_process_job)
{
    if (can_process_job) {
        jobPrinterId.Clear();
        jobCommand.Clear();
        jobLocalFilePath.Clear();
        // jobId = ""; // TODO: add this here when we have it.
        m_select_machine_dlg = nullptr;
    }
    m_can_process_job = can_process_job;
}

json PrintagoPanel::MachineObjectToJson(MachineObject *machine)
{
    json j;
    if (machine) {
        j["hardware"]["dev_model"]        = machine->printer_type;
        j["hardware"]["dev_display_name"] = machine->get_printer_type_display_str().ToStdString();
        j["hardware"]["dev_name"]         = machine->dev_name;
        j["hardware"]["nozzle_diameter"]  = machine->nozzle_diameter;

        j["connection_info"]["dev_ip"]              = machine->dev_ip;
        j["connection_info"]["dev_id"]              = machine->dev_id;
        j["connection_info"]["dev_name"]            = machine->dev_name;
        j["connection_info"]["dev_connection_type"] = machine->dev_connection_type;
        j["connection_info"]["is_local"]            = machine->is_local();
        j["connection_info"]["is_connected"]        = machine->is_connected();
        j["connection_info"]["is_connecting"]       = machine->is_connecting();
        j["connection_info"]["is_online"]           = machine->is_online();
        j["connection_info"]["has_access_right"]    = machine->has_access_right();
        j["connection_info"]["ftp_folder"]          = machine->get_ftp_folder();
        j["connection_info"]["access_code"]         = machine->get_access_code();

        // MachineObject State Info
        j["state"]["can_print"]                  = machine->can_print();
        j["state"]["can_resume"]                 = machine->can_resume();
        j["state"]["can_pause"]                  = machine->can_pause();
        j["state"]["can_abort"]                  = machine->can_abort();
        j["state"]["is_in_printing"]             = machine->is_in_printing();
        j["state"]["is_in_prepare"]              = machine->is_in_prepare();
        j["state"]["is_printing_finished"]       = machine->is_printing_finished();
        j["state"]["is_in_extrusion_cali"]       = machine->is_in_extrusion_cali();
        j["state"]["is_extrusion_cali_finished"] = machine->is_extrusion_cali_finished();

        // Current Job/Print Info
        j["current"]["print_status"]    = machine->print_status;
        j["current"]["m_gcode_file"]    = machine->m_gcode_file;
        j["current"]["print_time_left"] = machine->mc_left_time;
        j["current"]["print_percent"]   = machine->mc_print_percent;
        j["current"]["print_stage"]     = machine->mc_print_stage;
        j["current"]["print_sub_stage"] = machine->mc_print_sub_stage;
        j["current"]["curr_layer"]      = machine->curr_layer;
        j["current"]["total_layers"]    = machine->total_layers;

        // Temperatures
        j["current"]["temperatures"]["nozzle_temp"]         = machine->nozzle_temp;
        j["current"]["temperatures"]["nozzle_temp_target"]  = machine->nozzle_temp_target;
        j["current"]["temperatures"]["bed_temp"]            = machine->bed_temp;
        j["current"]["temperatures"]["bed_temp_target"]     = machine->bed_temp_target;
        j["current"]["temperatures"]["chamber_temp"]        = machine->chamber_temp;
        j["current"]["temperatures"]["chamber_temp_target"] = machine->chamber_temp_target;
        j["current"]["temperatures"]["frame_temp"]          = machine->frame_temp;

        // Cooling
        j["current"]["cooling"]["heatbreak_fan_speed"] = machine->heatbreak_fan_speed;
        j["current"]["cooling"]["cooling_fan_speed"]   = machine->cooling_fan_speed;
        j["current"]["cooling"]["big_fan1_speed"]      = machine->big_fan1_speed;
        j["current"]["cooling"]["big_fan2_speed"]      = machine->big_fan2_speed;
        j["current"]["cooling"]["fan_gear"]            = machine->fan_gear;
    }
    return j;
}

json PrintagoPanel::GetMachineStatus(MachineObject *machine)
{
    json statusObject = json::object();
    json machineList  = json::array();

    if (!machine)
        return json::object();

    statusObject["can_process_job"]     = can_process_job();
    statusObject["current_job_id"]      = ""; // add later from command.
    statusObject["current_job_machine"] = jobPrinterId.ToStdString();

    machineList.push_back(MachineObjectToJson(machine));
    statusObject["machines"] = machineList;
    return statusObject;
}

json PrintagoPanel::GetMachineStatus(const wxString &printerId)
{
    if (!devManager)
        return json::object();
    return GetMachineStatus(devManager->get_my_machine(printerId.ToStdString()));
}

json PrintagoPanel::GetAllStatus()
{
    std::map<std::string, MachineObject *> machineMap;
    json                                   statusObject = json::object();
    json                                   machineList  = json::array();

    if (!devManager)
        return json::object();

    statusObject["can_process_job"]     = can_process_job();
    statusObject["current_job_id"]      = ""; // add later from command.
    statusObject["current_job_machine"] = jobPrinterId.ToStdString();

    machineMap = devManager->get_my_machine_list();
    for (auto &pair : machineMap) {
        machineList.push_back(MachineObjectToJson(pair.second));
    }
    statusObject["machines"] = machineList;
    return statusObject;
}

bool PrintagoPanel::DownloadFileFromURL(const wxString url, const wxFileName &localFilename)
{
    boost::filesystem::path target_path = fs::path(localFilename.GetFullPath().ToStdString());
    wxString                filename    = localFilename.GetFullName(); // just filename and extension
    bool                    cont        = true;
    bool                    download_ok = false;
    int                     retry_count = 0;
    int                     percent     = 1;
    const int               max_retries = 3;
    wxString                msg;

    wxGetApp().plater()->reset();

    /* prepare project and profile */
    boost::thread download_thread = Slic3r::create_thread(
        [&percent, &cont, &retry_count, max_retries, &msg, &target_path, &download_ok, url, &filename] {
            int          res = 0;
            unsigned int http_code;
            std::string  http_body;

            fs::path tmp_path = target_path;
            tmp_path.replace_extension(tmp_path.extension().string() + ".download");

            auto http = Http::get(url.ToStdString());

            while (cont && retry_count < max_retries) {
                retry_count++;
                http.on_progress([&percent, &cont, &msg](Http::Progress progress, bool &cancel) {
                        if (!cont)
                            cancel = true;
                        if (progress.dltotal != 0) {
                            percent = progress.dlnow * 100 / progress.dltotal;
                        }
                        msg = wxString::Format("Printago part file Downloaded %d%%", percent);
                    })
                    .on_error([&msg, &cont, &retry_count, max_retries](std::string body, std::string error, unsigned http_status) {
                        (void) body;
                        BOOST_LOG_TRIVIAL(error)
                            << boost::str(boost::format("Error getting: `%1%`: HTTP %2%, %3%") % body % http_status % error);

                        if (retry_count == max_retries) {
                            msg  = boost::str(boost::format("Error getting: `%1%`: HTTP %2%, %3%") % body % http_status % error);
                            cont = false;
                        }
                    })
                    .on_complete([&cont, &download_ok, tmp_path, target_path](std::string body, unsigned /* http_status */) {
                        fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
                        file.write(body.c_str(), body.size());
                        file.close();
                        fs::rename(tmp_path, target_path);
                        cont        = false;
                        download_ok = true;
                    })
                    .perform_sync();
            }
        });

    while (cont) {
        wxMilliSleep(50);
        if (download_ok)
            break;
    }

    if (download_thread.joinable())
        download_thread.join();

    return download_ok;
}

bool PrintagoPanel::SavePrintagoFile(const wxString url, wxString &localPath)
{
    wxURI    uri(url);
    wxString path = uri.GetPath();

    wxArrayString pathComponents = wxStringTokenize(path, "/");
    wxString      uriFileName;

    if (!pathComponents.IsEmpty()) {
        uriFileName = pathComponents.Last();
    } else {
        return false;
    }
    // Remove any query string from the filename
    size_t queryPos = uriFileName.find('?');
    if (queryPos != wxString::npos) {
        uriFileName = uriFileName.substr(0, queryPos);
    }
    // Construct the full path for the temporary file
    wxString   tempDir = wxGetApp().app_config->get("download_path");
    wxFileName filename(tempDir, uriFileName);

    if (DownloadFileFromURL(url, filename)) {
        wxLogMessage("File downloaded to: %s", filename.GetFullPath());
        localPath = filename.GetFullPath();
        return true;
    } else {
        localPath = "";
        return false;
    }
}

wxString PrintagoPanel::wxURLErrorToString(wxURLError error)
{
    switch (error) {
    case wxURL_NOERR: return wxString("No Error");
    case wxURL_SNTXERR: return wxString("Syntax Error");
    case wxURL_NOPROTO: return wxString("No Protocol");
    case wxURL_NOHOST: return wxString("No Host");
    case wxURL_NOPATH: return wxString("No Path");
    case wxURL_CONNERR: return wxString("Connection Error");
    case wxURL_PROTOERR: return wxString("Protocol Error");
    default: return wxString("Unknown Error");
    }
}

void PrintagoPanel::HandlePrintagoCommand(const PrintagoCommandEvent &event)
{
    wxString                commandType        = event.GetCommandType();
    wxString                action             = event.GetAction();
    wxStringToStringHashMap parameters         = event.GetParameters();
    wxString                originalCommandStr = event.GetOriginalCommandStr();
    wxString                actionDetail;

    wxLogMessage("HandlePrintagoCommand: {command: " + commandType + ", action: " + action + "}");
    MachineObject *printer     = {nullptr};
    auto           machineList = devManager->get_my_machine_list();

    if (!commandType.compare("status")) {
        if (!action.compare("get_machine_list")) {
            std::string username = wxGetApp().getAgent()->is_user_login() ? wxGetApp().getAgent()->get_user_name() :
                                                                            "[printago_slicer_id?]";
            SendResponseMessage(username, GetAllStatus(), originalCommandStr);
            return;
        } else {
            SendErrorMessage("", action, originalCommandStr, "invalid status action");
            wxLogMessage("PrintagoCommandError: Invalid status action: " + action);
            return;
        }
    }

    wxString printerId = parameters.count("printer_id") ? parameters["printer_id"] : "Unspecified";
    if (!printerId.compare("Unspecified")) {
        SendErrorMessage("", action, originalCommandStr, "no printer_id specified");
        wxLogMessage("PrintagoCommandError: No printer_id specified");
        return;
    }
    // Find the printer in the machine list
    auto it = std::find_if(machineList.begin(), machineList.end(),
                           [&printerId](const std::pair<std::string, MachineObject *> &pair) { return pair.second->dev_id == printerId; });

    if (it != machineList.end()) {
        // Printer found
        printer = it->second;
    } else {
        SendErrorMessage(printerId, action, originalCommandStr, "no printer not found with ID: " + printerId);
        wxLogMessage("PrintagoCommandError: No printer found with ID: " + printerId);
        return;
    }

    if (!commandType.compare("printer_control")) {
        if (!action.compare("pause_print")) {
            try {
                printer->command_task_pause();
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred issuing pause_print");
                return;
            }
        }
        else if (!action.compare("resume_print")) {
            try {
                if (printer->can_resume()) {
                    printer->command_task_resume();
                } else {
                    SendErrorMessage(printerId, action, originalCommandStr, "cannot resume print");
                    return;
                }
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred issuing resume_print");
                return;
            }
        }
        else if (!action.compare("stop_print")) {
            try {
                printer->command_task_abort();
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred issuing stop_print");
                return;
            }
        }
        else if (!action.compare("get_status")) {
            SendStatusMessage(printerId, GetMachineStatus(printer), originalCommandStr);
            return;
        }
        else if (!action.compare("start_print_bbl")) {
            wxString printagoFileUrl = parameters["url"];
            wxString decodedUrl      = {""};
            jobPrinterId             = printerId;
            jobCommand               = originalCommandStr;

            if (!m_select_machine_dlg)
                m_select_machine_dlg = new SelectMachineDialog(wxGetApp().plater());

            if (!can_process_job()) {
                SendErrorMessage(printerId, action, originalCommandStr, "busy with current job - check status");
                return;
            }
            set_can_process_job(false);

            if (printagoFileUrl.empty()) {
                SendErrorAndUnblock(printerId, action, originalCommandStr, "no url specified");
                return;
            } else {
                decodedUrl = Http::url_decode(printagoFileUrl.ToStdString());
                // TODO: validate that the URL is valid w/ regex.
            }

            if (SavePrintagoFile(decodedUrl, jobLocalFilePath)) {
                wxLogMessage("Downloaded file to: " + jobLocalFilePath);
            } else {
                SendErrorAndUnblock(printerId, action, originalCommandStr, "download failed");
                return;
            }

            // TODO try/catch this if block.
            try {
                wxFileName filename(jobLocalFilePath);
                if (!filename.GetExt().MakeUpper().compare("3MF")) {
                    // The last 'true' tells the function to not ask the user to confirm the load; save any existing work.
                    wxGetApp().plater()->load_project(jobLocalFilePath, "-", true);
                } else {
                    std::vector<std::string> filePathArray;
                    filePathArray.push_back(jobLocalFilePath.ToStdString());
                    LoadStrategy strategy = LoadStrategy::LoadModel | LoadStrategy::LoadConfig | LoadStrategy::LoadAuxiliary |
                                            LoadStrategy::Silence;
                    wxGetApp().plater()->load_files(filePathArray, strategy, false);
                }
                wxGetApp().plater()->select_plate(0, true);
            } catch (...) {
                SendErrorAndUnblock(printerId, action, originalCommandStr, "an error occurred loading the file");
                return;
            }
            wxGetApp().plater()->reslice();
            actionDetail = "";

        } else {
            SendErrorMessage(printerId, action, originalCommandStr, "invalid printer_control action");
            return;
        }

        // Seems like a hack, but we can't send messages inside that if/else block.
        // So keep the message back the same for the other commands in printer_control.
        if (!action.compare("start_print_bbl")) {
            actionDetail = "downloaded successfully: " + jobLocalFilePath;
            SendSuccessMessage(printerId, action + ":download_file", originalCommandStr, actionDetail);
            actionDetail = "model loaded: " + jobLocalFilePath; // or project after 3ML load_project.
            SendSuccessMessage(printerId, action + ":load_file", originalCommandStr, actionDetail);
            actionDetail = "slicing started: " + jobLocalFilePath;
            SendSuccessMessage(printerId, action + ":start_slice", originalCommandStr, actionDetail);
        } else {
            SendSuccessMessage(printerId, action, originalCommandStr, actionDetail);
        }

    }
    else if (!commandType.compare("temperature_control")) {
        wxString tempStr = parameters["temperature"];
        long     targetTemp;
        if (!tempStr.ToLong(&targetTemp)) {
            SendErrorMessage(printerId, action, originalCommandStr, "invalid temperature value");
            return;
        }

        if (!action.compare("set_hotend")) {
            try {
                printer->command_set_nozzle(targetTemp);
                actionDetail = wxString::Format("%d", targetTemp);
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred setting nozzle temperature");
                return;
            }
        } else if (!action.compare("set_bed")) {
            try {
                int limit = printer->get_bed_temperature_limit();
                if (targetTemp >= limit) {
                    targetTemp = limit;
                }
                printer->command_set_bed(targetTemp);
                actionDetail = wxString::Format("%d", targetTemp);
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred setting bed temperature");
                return;
            }
        } else {
            SendErrorMessage(printerId, action, originalCommandStr, "invalid temperature_control action");
            wxLogMessage("PrintagoCommandError: Invalid temperature_control action: " + action);
            return;
        }

        SendSuccessMessage(printerId, action, originalCommandStr, actionDetail);

    }
    else if (!commandType.compare("movement_control")) {
        if (!action.compare("jog")) {
            auto axes = ExtractPrefixedParams(parameters, "axes");
            if (axes.empty()) {
                SendErrorMessage(printerId, action, originalCommandStr, "no axes specified");
                wxLogMessage("PrintagoCommandError: No axes specified");
                return;
            }

            if (!printer->is_axis_at_home("X") || !printer->is_axis_at_home("Y") || !printer->is_axis_at_home("Z")) {
                SendErrorMessage(printerId, action, originalCommandStr, "must home axes before moving");
                wxLogMessage("PrintagoCommandError: Axes not at home");
                return;
            }
            // Iterate through each axis and its value; we do this loop twice to ensure the input in clean.
            // this ensures we do not move the head unless all input moves are valid.
            for (const auto &axis : axes) {
                wxString axisName = axis.first;
                axisName.MakeUpper();
                if (axisName != "X" && axisName != "Y" && axisName != "Z") {
                    SendErrorMessage(printerId, action, originalCommandStr, "invalid axis name: " + axisName);
                    wxLogMessage("PrintagoCommandError: Invalid axis name " + axisName);
                    return;
                }
                wxString axisValueStr = axis.second;
                double   axisValue;
                if (!axisValueStr.ToDouble(&axisValue)) {
                    SendErrorMessage(printerId, action, originalCommandStr, "invalid value for axis " + axisName);
                    wxLogMessage("PrintagoCommandError: Invalid value for axis " + axisName);
                    return;
                }
            }

            for (const auto &axis : axes) {
                wxString axisName = axis.first;
                axisName.MakeUpper();
                wxString axisValueStr = axis.second;
                double   axisValue;
                axisValueStr.ToDouble(&axisValue);
                try {
                    printer->command_axis_control(axisName.ToStdString(), 1.0, axisValue, 3000);
                } catch (...) {
                    SendErrorMessage(printerId, action, originalCommandStr, "an error occurred moving axis " + axisName);
                    wxLogMessage("PrintagoCommandError: An error occurred moving axis " + axisName);
                    return;
                }
            }

        } else if (!action.compare("home")) {
            try {
                printer->command_go_home();
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred homing axes");
                wxLogMessage("PrintagoCommandError: An error occurred homing axes");
                return;
            }

        } else if (!action.compare("extrude")) {
            wxString amtStr = parameters["amount"];
            long     extrudeAmt;
            if (!amtStr.ToLong(&extrudeAmt)) {
                wxLogMessage("Invalid extrude amount value: " + amtStr);
                SendErrorMessage(printerId, action, originalCommandStr, "invalid extrude amount value");
                return;
            }

            if (printer->nozzle_temp >= PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL) {
                try {
                    printer->command_axis_control("E", 1.0, extrudeAmt, 900);
                    actionDetail = wxString::Format("%d", extrudeAmt);
                } catch (...) {
                    SendErrorMessage(printerId, action, originalCommandStr, "an error occurred extruding filament");
                    wxLogMessage("PrintagoCommandError: An error occurred extruding filament");
                    return;
                }
            } else {
                SendErrorMessage(printerId, action, originalCommandStr,
                                 wxString::Format("nozzle temperature too low to extrude (min: %.1f)",
                                                  PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL));
                wxLogMessage("PrintagoCommandError: Nozzle temperature too low to extrude");
                return;
            }

        } else {
            SendErrorMessage(printerId, action, originalCommandStr, "invalid movement_control action");
            wxLogMessage("PrintagoCommandError: Invalid movement_control action");
            return;
        }

        SendSuccessMessage(printerId, action, originalCommandStr, actionDetail);
    }
    return;
}

void PrintagoPanel::OnSlicingProcessCompleted(SlicingProcessCompletedEvent &event)
{
    // in case we got here by mistake and there's nothing we're trying to process.
    if (jobPrinterId.IsEmpty() || !m_select_machine_dlg || can_process_job()) {
        set_can_process_job(true);
        return;
    }
    if (!event.success()) {
        wxString actionDetail = "Slicing Unknown Error: " + jobLocalFilePath;
        if (event.cancelled())
            actionDetail = "slicing cancelled: " + jobLocalFilePath;
        else if (event.error())
            actionDetail = "slicing error: " + jobLocalFilePath;
        SendErrorAndUnblock(jobPrinterId, "start_print_bbl", jobCommand, actionDetail);
        return;
    }

    wxString actionDetail = "slicing complete: " + jobLocalFilePath;
    SendSuccessMessage(jobPrinterId, "start_print_bbl:slice_complete", jobCommand, actionDetail);

    // Slicing Success -> Send to the Printer

    m_select_machine_dlg->set_print_type(PrintFromType::FROM_NORMAL);
    m_select_machine_dlg->prepare(0);
    devManager->set_selected_machine(jobPrinterId.ToStdString(), false);
    m_select_machine_dlg->setPrinterLastSelect(jobPrinterId.ToStdString());

    wxCommandEvent evt(GetId());
    m_select_machine_dlg->on_ok_btn(evt);

    actionDetail = "Job Sent to Printer: " + jobLocalFilePath;
    SendSuccessMessage(jobPrinterId, "start_print_bbl", jobCommand, actionDetail);
    set_can_process_job(true);
}

wxStringToStringHashMap PrintagoPanel::ParseQueryString(const wxString &queryString)
{
    wxStringToStringHashMap params;

    // Split the query string on '&' to get key-value pairs
    wxStringTokenizer tokenizer(queryString, "&");
    while (tokenizer.HasMoreTokens()) {
        wxString token = tokenizer.GetNextToken();

        // Split each key-value pair on '='
        wxString key   = token.BeforeFirst('=');
        wxString value = token.AfterFirst('=');

        // URL-decode the key and value
        wxString decodedKey   = wxURI::Unescape(key);
        wxString decodedValue = wxURI::Unescape(value);

        params[decodedKey] = decodedValue;
    }
    return params;
}

std::map<wxString, wxString> PrintagoPanel::ExtractPrefixedParams(const wxStringToStringHashMap &params, const wxString &prefix)
{
    std::map<wxString, wxString> extractedParams;
    for (const auto &kv : params) {
        if (kv.first.StartsWith(prefix + ".")) {
            wxString parmName         = kv.first.Mid(prefix.length() + 1); // +1 for the dot
            extractedParams[parmName] = kv.second;
        }
    }
    return extractedParams;
}

void PrintagoPanel::SendStatusMessage(const wxString &printer_id, const json &statusData, const wxString &command)
{
    // SendJsonMessage("status", printer_id, statusData, command);
    PrintagoMessageEvent event;
    event.SetMessageType("status");
    event.SetPrinterId(printer_id);
    event.SetCommand(command);
    event.SetData(statusData);

    wxPostEvent(this, event);
}

void PrintagoPanel::SendResponseMessage(const wxString &printer_id, const json &responseData, const wxString &command)
{
    // SendJsonMessage("response", printer_id, responseData, command);
    PrintagoMessageEvent event;
    event.SetMessageType("status");
    event.SetPrinterId(printer_id);
    event.SetCommand(command);
    event.SetData(responseData);

    wxPostEvent(this, event);
}

void PrintagoPanel::SendSuccessMessage(const wxString &printer_id,
                                       const wxString &localCommand,
                                       const wxString &command,
                                       const wxString &localCommandDetail)
{
    json responseData;
    responseData["local_command"]        = localCommand.ToStdString();
    responseData["local_command_detail"] = localCommandDetail.ToStdString();
    responseData["success"]              = true;

    PrintagoMessageEvent event;
    event.SetMessageType("success");
    event.SetPrinterId(printer_id);
    event.SetCommand(command);
    event.SetData(responseData);

    wxPostEvent(this, event);
}

void PrintagoPanel::SendErrorMessage(const wxString &printer_id,
                                     const wxString &localCommand,
                                     const wxString &command,
                                     const wxString &errorDetail)
{
    json errorResponse;
    errorResponse["local_command"] = localCommand.ToStdString();
    errorResponse["error_detail"]  = errorDetail.ToStdString();
    errorResponse["success"]       = false;

    PrintagoMessageEvent event;
    event.SetMessageType("error");
    event.SetPrinterId(printer_id);
    event.SetCommand(command);
    event.SetData(errorResponse);

    wxPostEvent(this, event);
}

void PrintagoPanel::SendErrorAndUnblock(const wxString &printer_id,
                                        const wxString &localCommand,
                                        const wxString &command,
                                        const wxString &errorDetail)
{
    set_can_process_job(true);
    SendErrorMessage(printer_id, localCommand, command, errorDetail);
}

void PrintagoPanel::OnPrintagoSendWebViewMessage(PrintagoMessageEvent &event)
{
    // SendJsonMessage(event.GetMessageType(), event.GetPrinterId(), event.GetData(), event.GetCommand());
    wxDateTime now = wxDateTime::Now();
    now.MakeUTC();
    const wxString timestamp = now.FormatISOCombined() + "Z";

    json message;
    message["type"]        = event.GetMessageType().ToStdString();
    message["timestamp"]   = timestamp.ToStdString();
    message["printer_id"]  = event.GetPrinterId().ToStdString();
    message["client_type"] = "bambu";
    message["command"]     = event.GetCommand().ToStdString();
    message["data"]        = event.GetData();

    const wxString messageStr = wxString(message.dump().c_str(), wxConvUTF8);
    CallAfter([this, messageStr] { m_browser->RunScript(wxString::Format("window.postMessage(%s, '*');", messageStr)); });
}

void PrintagoPanel::OnNavigationRequest(wxWebViewEvent &evt)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();

    const wxString &url = evt.GetURL();

    if (url.StartsWith("printago://")) {
        evt.Veto(); // Prevent the web view from navigating to this URL

        wxURI         uri(url);
        wxString      path           = uri.GetPath();
        wxArrayString pathComponents = wxStringTokenize(path, "/");
        wxString      commandType, action;

        // Extract commandType and action from the path
        if (pathComponents.GetCount() >= 2) {
            commandType = pathComponents.Item(1); // The first actual component after the leading empty one
            action      = pathComponents.Item(2); // The second actual component
        } else {
            // Handle error: insufficient components in the path
            return;
        }

        wxString                query      = uri.GetQuery();          // Get the query part of the URI
        wxStringToStringHashMap parameters = ParseQueryString(query); // Use ParseQueryString to get parameters

        PrintagoCommandEvent event;
        event.SetCommandType(commandType);
        event.SetAction(action);
        event.SetParameters(parameters);
        event.SetOriginalCommandStr(url.ToStdString());

        wxPostEvent(this, event);
    }

    if (m_info->IsShown()) {
        m_info->Dismiss();
    }
}

void PrintagoPanel::OnNavigationComplete(wxWebViewEvent &evt)
{
    m_browser->Show();
    Layout();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Navigation complete; url='" + evt.GetURL() + "'");
}

void PrintagoPanel::OnDocumentLoaded(wxWebViewEvent &evt)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    // Only notify if the document is the main frame, not a subframe
    if (evt.GetURL() == m_browser->GetCurrentURL()) {
        if (wxGetApp().get_mode() == comDevelop)
            wxLogMessage("%s", "Document loaded; url='" + evt.GetURL() + "'");
    }
}

void PrintagoPanel::OnNewWindow(wxWebViewEvent &evt) { evt.Veto(); }

void PrintagoPanel::RunScript(const wxString &javascript)
{
    if (!m_browser)
        return;

    WebView::RunScript(m_browser, javascript);
}

void PrintagoPanel::OnError(wxWebViewEvent &evt)
{
#define WX_ERROR_CASE(type) \
    case type: category = #type; break;

    wxString category;
    switch (evt.GetInt()) {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": [" << category << "] " << evt.GetString().ToUTF8().data();

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Error; url='" + evt.GetURL() + "', error='" + category + " (" + evt.GetString() + ")'");

    // Show the info bar with an error
    m_info->ShowMessage(_L("An error occurred loading ") + evt.GetURL() + "\n" + "'" + category + "'", wxICON_ERROR);
}

}} // namespace Slic3r::GUI
