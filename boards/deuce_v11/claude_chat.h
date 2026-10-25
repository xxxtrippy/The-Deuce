#pragma once
// THE DEUCE v1.1 -- text-only Claude chat over the Anthropic Messages API.
// Launched from the "Claude" main-menu tile; settings also reachable from Config.
// Text in / text out only: no images, no tool use, no rendering.

void deuceClaudeLaunch();   // tile entry: compose / new chat / read last / settings
void deuceClaudeSettings(); // key + model + max-tokens + clear chat (also under Config)
