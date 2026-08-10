"""Round 中可在下一個 Step 套用的輸入與 ask 設定。"""


class PendingInputs:
    def _init_inputs(self):
        self._say = []
        self._images = []
        self._tool_updates = []
        self._ask_options = {}
        self._ask_updates = {}

    def say(self, text):
        """在下一個 Step 插入一段文字；多筆依 FIFO 合併。"""
        if not text:
            return
        with self._lock:
            self._accept_input_unlocked()
            self._say.append(str(text))

    def add_instruction(self, text):
        """`say()` 的明確名稱。"""
        return self.say(text)

    def add_images(self, *images):
        """在下一個 Step 加入圖片路徑或 URL。"""
        if len(images) == 1 and isinstance(images[0], (list, tuple)):
            images = tuple(images[0])
        images = tuple(str(image) for image in images if image)
        if not images:
            return
        with self._lock:
            self._accept_input_unlocked()
            self._images.extend(images)

    def add_tools(self, schemas, dispatch):
        """在下一個 Step 原子加入／替換 tool schemas 與同名實作。"""
        schemas = [schemas] if isinstance(schemas, dict) else list(schemas or [])
        dispatch = dict(dispatch or {})
        names = [_tool_name(schema) for schema in schemas]
        if any(name is None for name in names) or len(names) != len(set(names)):
            raise ValueError("tool schemas must have unique function names")
        if set(names) != set(dispatch):
            raise ValueError("tool schemas and dispatch must contain the same names")
        if not schemas:
            return
        with self._lock:
            self._accept_input_unlocked()
            self._tool_updates.append((schemas, dispatch))

    def set_ask_options(self, **options):
        """從下一個 Step 起覆寫 ask() 選項；傳 None 可清掉一項。"""
        if not options:
            return
        reserved = {"prompt", "images", "tool_results", "remember"} & set(options)
        if reserved:
            raise ValueError(f"agentloop owns ask options: {', '.join(sorted(reserved))}")
        with self._lock:
            self._accept_input_unlocked()
            self._ask_updates.update(options)

    def _accept_input_unlocked(self):
        if self.stop is not None:
            raise RuntimeError("round already completed")
        if self.stopping:
            raise RuntimeError("round is stopping")


def _tool_name(schema):
    try:
        return schema["function"]["name"]
    except (KeyError, TypeError):
        return None


def apply_tool_updates(bot, dispatch, updates):
    """在 Step dispatch 前套用同一批 schema + implementation 更新。"""
    if not updates:
        return
    current = list(getattr(bot, "tools", None) or [])
    by_name = {_tool_name(schema): schema for schema in current}
    for schemas, functions in updates:
        for schema in schemas:
            by_name[_tool_name(schema)] = schema
        dispatch.update(functions)
    bot.tools = list(by_name.values())
