#pragma once

#include "core/platform/platform.h"

namespace eui::platform {

using core::platform::FileDialogOptions;
using core::platform::FileDialogResult;
using core::platform::FileDialogStatus;
using core::platform::TrayMenuItem;
using core::platform::TrayMenuRequestedHandler;
using core::platform::TrayOptions;
using core::platform::chooseFile;
using core::platform::chooseFiles;
using core::platform::openFileDialog;
using core::platform::openUrl;
using core::platform::resolveResourcePath;
using core::platform::setTrayMenu;
using core::platform::setTrayMenuRequestedHandler;

} // namespace eui::platform
