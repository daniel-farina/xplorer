// XPLORER: first-run provisioning of the grok CLI config (see header).
#include "chrome/browser/agent_gateway/grok_provisioner.h"

#include <cstdlib>
#include <string>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"

namespace agent_gateway {
namespace {

// The xplorer MCP server script is Python; the launch command differs by OS —
// Windows ships "python" (python.org) rather than "python3".
#if BUILDFLAG(IS_WIN)
constexpr char kPython[] = "python";
#else
constexpr char kPython[] = "python3";
#endif

base::FilePath HomeDir() {
  base::FilePath home;
  base::PathService::Get(base::DIR_HOME, &home);
  return home;
}

// Locate the bundled sdk/ dir holding the MCP server script (xplorer_mcp.py).
// Mirrors the companion-UI resolver: env override -> the app bundle
// (Contents/Resources/sdk on mac, exe_dir/sdk elsewhere) -> a dev checkout.
base::FilePath SdkDir() {
  if (const char* env = std::getenv("XPLORER_SDK"); env && *env)
    return base::FilePath::FromUTF8Unsafe(env);
  base::FilePath dir;
#if BUILDFLAG(IS_WIN)
  // Installer layout: ships beside chrome.dll in the versioned dir (DIR_MODULE).
  if (base::PathService::Get(base::DIR_MODULE, &dir)) {
    base::FilePath c = dir.AppendASCII("sdk");
    if (base::DirectoryExists(c))
      return c;
  }
#endif
  if (base::PathService::Get(base::DIR_EXE, &dir)) {
#if BUILDFLAG(IS_MAC)
    base::FilePath c = dir.DirName().AppendASCII("Resources").AppendASCII("sdk");
#else
    base::FilePath c = dir.AppendASCII("sdk");
#endif
    if (base::DirectoryExists(c))
      return c;
  }
  base::FilePath dev = HomeDir()
                           .AppendASCII("cli_experiment")
                           .AppendASCII("xplorer")
                           .AppendASCII("sdk");
  if (base::DirectoryExists(dev))
    return dev;
  return base::FilePath();
}

std::string XplorerBlock(const std::string& sdk) {
  return base::StringPrintf(
      "\n[mcp_servers.xplorer]\ncommand = \"%s\"\n"
      "args = [\"%s/xplorer_mcp.py\"]\nenabled = true\n",
      kPython, sdk.c_str());
}

}  // namespace

void ProvisionGrok() {
  base::FilePath sdk = SdkDir();
  if (sdk.empty())
    return;  // No MCP server to point at; nothing safe to write.
  const std::string sdk_s = sdk.AsUTF8Unsafe();

  base::FilePath grok_dir = HomeDir().AppendASCII(".grok");
  if (!base::CreateDirectory(grok_dir))
    return;
  base::FilePath cfg = grok_dir.AppendASCII("config.toml");

  if (!base::PathExists(cfg)) {
    // Fresh machine: write a complete minimal config wired to the xplorer MCP.
    std::string full =
        "[cli]\ninstaller = \"internal\"\nauto_update = false\n\n"
        "[ui]\npermission_mode = \"always-approve\"\n\n"
        "[features]\ncodebase_indexing = true\n";
    full += XplorerBlock(sdk_s);
    full += "\n[models]\ndefault = \"grok-composer-2.5-fast\"\n";
    base::WriteFile(cfg, full);
    return;
  }

  // Existing config: append ONLY the missing xplorer MCP block; never clobber
  // the user's own edits.
  std::string existing;
  if (!base::ReadFileToString(cfg, &existing))
    return;
  if (existing.find("[mcp_servers.xplorer]") == std::string::npos)
    base::AppendToFile(cfg, XplorerBlock(sdk_s));
}

}  // namespace agent_gateway
