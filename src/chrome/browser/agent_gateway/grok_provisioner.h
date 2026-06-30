// XPLORER: first-run provisioning of the grok CLI config.
#ifndef CHROME_BROWSER_AGENT_GATEWAY_GROK_PROVISIONER_H_
#define CHROME_BROWSER_AGENT_GATEWAY_GROK_PROVISIONER_H_

namespace agent_gateway {

// Xplor is distributed to machines that may have neither the grok CLI nor a
// ~/.grok/config.toml; without a config, the sidebar agent and every X feature
// fail. ProvisionGrok() makes the browser self-configuring:
//   * if ~/.grok/config.toml is MISSING, it writes a complete minimal config
//     wired to the bundled MCP servers (xplorer + x-mock + x-docs + xapi);
//   * if it EXISTS, it appends ONLY the missing Xplor [mcp_servers.*] blocks,
//     never clobbering the user's own edits.
// Idempotent and cheap; safe to call on every startup. Call before the gateway
// or grok is used (PostBrowserStart).
void ProvisionGrok();

}  // namespace agent_gateway

#endif  // CHROME_BROWSER_AGENT_GATEWAY_GROK_PROVISIONER_H_
