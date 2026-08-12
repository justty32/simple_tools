"""Names preloaded by ``python -m shells repl``."""

import agentloop
import base_tools
import llms
from agentloop import Controller, Handle
from llms import Engine, LLM, Params
from shells import Assistant, assistant, session, toolbox

print(
    "已就緒: LLM, Engine, Params, Controller, Handle, Assistant, "
    "assistant, toolbox, session; "
    "以及 llms, base_tools, agentloop"
)
print(f"工具 workspace: {base_tools.get_root()}")
