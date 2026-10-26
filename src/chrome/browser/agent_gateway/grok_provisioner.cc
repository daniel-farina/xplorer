// XPLORER: first-run provisioning of the grok CLI config (see header).
#include "chrome/browser/agent_gateway/grok_provisioner.h"

#include <cstdlib>
#include <string>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/path_service.h"
#include "base/strings/stringprintf.h"
#include "base/task/thread_pool.h"
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

// Escapes a string for a TOML basic ("...") string. Windows paths contain
// backslashes, and an unescaped "C:\Users\..." is invalid TOML ("\U" starts a
// unicode escape), which would make grok reject the whole config file.
std::string TomlQuote(const std::string& in) {
  std::string out = "\"";
  for (char c : in) {
    if (c == '\\' || c == '"')
      out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

// Marks a block this provisioner wrote, so later startups may refresh its
// script path (the Windows install dir is versioned and changes on every
// update; a mac/linux app can be moved). Blocks without it are the user's own
// and are never touched.
constexpr char kManagedMarker[] =
    "# Managed by Xplor: the args path is refreshed on startup.";

std::string ArgsLine(const base::FilePath& sdk) {
  const std::string script = sdk.AppendASCII("xplorer_mcp.py").AsUTF8Unsafe();
  return "args = [" + TomlQuote(script) + "]";
}

std::string XplorerBlock(const base::FilePath& sdk) {
  return base::StringPrintf(
      "\n[mcp_servers.xplorer]\n%s\ncommand = \"%s\"\n%s\nenabled = true\n",
      kManagedMarker, kPython, ArgsLine(sdk).c_str());
}

// Blocking file IO; runs on the thread pool (MayBlock), never the UI thread.
void ProvisionGrokOnPool() {
  base::FilePath sdk = SdkDir();
  if (sdk.empty())
    return;  // No MCP server to point at; nothing safe to write.

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
    full += XplorerBlock(sdk);
    full += "\n[models]\ndefault = \"grok-composer-2.5-fast\"\n";
    base::WriteFile(cfg, full);
    return;
  }

  // Existing config: append ONLY the missing xplorer MCP block; never clobber
  // the user's own edits.
  std::string existing;
  if (!base::ReadFileToString(cfg, &existing))
    return;
  if (existing.find("[mcp_servers.xplorer]") == std::string::npos) {
    base::AppendToFile(cfg, XplorerBlock(sdk));
    return;
  }
  // Our own block from an earlier run: point it at the current sdk dir.
  const size_t marker = existing.find(kManagedMarker);
  if (marker == std::string::npos)
    return;
  const size_t args = existing.find("\nargs = ", marker);
  const size_t next_table = existing.find("\n[", marker);
  if (args == std::string::npos ||
      (next_table != std::string::npos && args > next_table)) {
    return;  // The user edited our block; leave it alone.
  }
  size_t eol = existing.find('\n', args + 1);
  if (eol == std::string::npos)
    eol = existing.size();
  const std::string want = ArgsLine(sdk);
  if (existing.compare(args + 1, eol - args - 1, want) == 0)
    return;
  existing.replace(args + 1, eol - args - 1, want);
  base::WriteFile(cfg, existing);
}

}  // namespace

void ProvisionGrok() {
  // Called from PostBrowserStart on the UI thread, where blocking IO is not
  // allowed; grok is only spawned on a later user action, so best effort is
  // early enough.
  base::ThreadPool::PostTask(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::BEST_EFFORT},
      base::BindOnce(&ProvisionGrokOnPool));
}

}  // namespace agent_gateway
