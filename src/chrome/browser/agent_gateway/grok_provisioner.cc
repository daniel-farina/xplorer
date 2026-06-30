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

base::FilePath HomeDir() {
  base::FilePath home;
  base::PathService::Get(base::DIR_HOME, &home);
  return home;
}

// Locate the bundled sdk/ dir holding the MCP server scripts (xplorer_mcp.py,
// mock_x_mcp.py). Mirrors the companion-UI resolver: env override -> the app
// bundle (Contents/Resources/sdk on mac, exe_dir/sdk elsewhere) -> a dev checkout.
base::FilePath SdkDir() {
  if (const char* env = std::getenv("XPLORER_SDK"); env && *env)
    return base::FilePath::FromUTF8Unsafe(env);
  base::FilePath exe_dir;
  if (base::PathService::Get(base::DIR_EXE, &exe_dir)) {
#if BUILDFLAG(IS_MAC)
    base::FilePath bundled =
        exe_dir.DirName().AppendASCII("Resources").AppendASCII("sdk");
#else
    base::FilePath bundled = exe_dir.AppendASCII("sdk");
#endif
    if (base::DirectoryExists(bundled))
      return bundled;
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
      "\n[mcp_servers.xplorer]\ncommand = \"python3\"\n"
      "args = [\"%s/xplorer_mcp.py\"]\nenabled = true\n",
      sdk.c_str());
}
std::string XMockBlock(const std::string& sdk) {
  return base::StringPrintf(
      "\n[mcp_servers.x-mock]\ncommand = \"python3\"\n"
      "args = [\"%s/mock_x_mcp.py\"]\nenabled = true\n",
      sdk.c_str());
}
constexpr char kXDocsBlock[] =
    "\n[mcp_servers.x-docs]\nurl = \"https://docs.x.com/mcp\"\nenabled = true\n";
constexpr char kXapiBlock[] =
    "\n[mcp_servers.xapi]\ncommand = \"npx\"\n"
    "args = [\"-y\", \"@xdevplatform/xurl\", \"mcp\", \"https://api.x.com/mcp\"]\n"
    "enabled = false\n";

}  // namespace

void ProvisionGrok() {
  base::FilePath sdk = SdkDir();
  if (sdk.empty())
    return;  // No MCP servers to point at; nothing safe to write.
  const std::string sdk_s = sdk.AsUTF8Unsafe();

  base::FilePath grok_dir = HomeDir().AppendASCII(".grok");
  if (!base::CreateDirectory(grok_dir))
    return;
  base::FilePath cfg = grok_dir.AppendASCII("config.toml");

  if (!base::PathExists(cfg)) {
    // Fresh machine: write a complete minimal config wired to the MCP servers.
    std::string full =
        "[cli]\ninstaller = \"internal\"\nauto_update = false\n\n"
        "[ui]\npermission_mode = \"always-approve\"\n\n"
        "[features]\ncodebase_indexing = true\n";
    full += XplorerBlock(sdk_s);
    full += XMockBlock(sdk_s);
    full += kXDocsBlock;
    full += kXapiBlock;
    full += "\n[models]\ndefault = \"grok-composer-2.5-fast\"\n";
    base::WriteFile(cfg, full);
    return;
  }

  // Existing config: append ONLY the missing Xplor MCP blocks; never clobber
  // the user's edits.
  std::string existing;
  if (!base::ReadFileToString(cfg, &existing))
    return;
  std::string add;
  if (existing.find("[mcp_servers.xplorer]") == std::string::npos)
    add += XplorerBlock(sdk_s);
  if (existing.find("[mcp_servers.x-mock]") == std::string::npos)
    add += XMockBlock(sdk_s);
  if (existing.find("[mcp_servers.x-docs]") == std::string::npos)
    add += kXDocsBlock;
  if (existing.find("[mcp_servers.xapi]") == std::string::npos)
    add += kXapiBlock;
  if (!add.empty())
    base::AppendToFile(cfg, add);
}

}  // namespace agent_gateway
