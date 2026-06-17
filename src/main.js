const { listen } = window.__TAURI__.event;
const { invoke } = window.__TAURI__.core;

// ── Status helper ─────────────────────────────────────────────────────────────
function setStatus(msg, type = "") {
  const el = document.getElementById("ocr-status");
  el.innerHTML = msg;
  el.className = "status" + (type ? " " + type : "");
}

// ── Init ──────────────────────────────────────────────────────────────────────
window.addEventListener("DOMContentLoaded", async () => {
  // ── Hotkey recorder configuration ──────────────────────────────────────────
  const hotkeyInput = document.getElementById("hotkey-input");
  const saveBtn = document.getElementById("btn-save-hotkey");
  const cancelBtn = document.getElementById("btn-cancel-hotkey");

  let isRecording = false;
  let originalHotkey = "";

  // Fetch initial hotkey
  try {
    const currentHotkey = await invoke("get_hotkey");
    hotkeyInput.value = currentHotkey || "None";
  } catch (err) {
    console.error("Failed to load hotkey:", err);
  }

  hotkeyInput.addEventListener("click", () => {
    if (!isRecording) {
      isRecording = true;
      originalHotkey = hotkeyInput.value;
      hotkeyInput.value = "Press keys...";
      saveBtn.classList.remove("hidden");
      cancelBtn.classList.remove("hidden");
    }
  });

  hotkeyInput.addEventListener("keydown", (e) => {
    if (!isRecording) return;
    e.preventDefault();
    e.stopPropagation();

    const keys = [];
    if (e.ctrlKey) keys.push("Ctrl");
    if (e.shiftKey) keys.push("Shift");
    if (e.altKey) keys.push("Alt");
    if (e.metaKey) keys.push("Super");

    // Capture primary key
    let key = e.key;
    if (key === "Control" || key === "Shift" || key === "Alt" || key === "Meta") {
      if (keys.length > 0) {
        hotkeyInput.value = keys.join("+");
      } else {
        hotkeyInput.value = "Press keys...";
      }
      return;
    }

    // Map keys to standard strings
    if (key === " ") {
      key = "Space";
    } else if (key.length === 1) {
      key = key.toUpperCase();
    } else {
      // Capitalize first letter of special keys (e.g. ArrowUp, Escape)
      key = key.charAt(0).toUpperCase() + key.slice(1);
    }

    keys.push(key);
    hotkeyInput.value = keys.join("+");
    
    // Auto blur/stop recording when primary key is pressed
    isRecording = false;
    hotkeyInput.blur();
  });

  saveBtn.addEventListener("click", async () => {
    isRecording = false;
    const newHotkey = hotkeyInput.value;
    if (newHotkey === "Press keys..." || !newHotkey) {
      hotkeyInput.value = originalHotkey;
      saveBtn.classList.add("hidden");
      cancelBtn.classList.add("hidden");
      return;
    }

    if (newHotkey !== "None") {
      const parts = newHotkey.split("+");
      const modifiers = ["Ctrl", "Shift", "Alt", "Super"];
      if (parts.length !== 2 || !modifiers.includes(parts[0]) || modifiers.includes(parts[1])) {
        setStatus("Error: Hotkey must be a combination of exactly 2 keys (1 modifier + 1 key, e.g., Ctrl+Space)", "error");
        hotkeyInput.value = originalHotkey;
        saveBtn.classList.add("hidden");
        cancelBtn.classList.add("hidden");
        return;
      }
    }

    setStatus("Saving hotkey...");
    try {
      await invoke("set_hotkey", { newHotkey });
      setStatus(`Hotkey updated to <strong>${newHotkey}</strong> ✓`);
      saveBtn.classList.add("hidden");
      cancelBtn.classList.add("hidden");
    } catch (err) {
      setStatus("Error: " + err, "error");
      hotkeyInput.value = originalHotkey;
      saveBtn.classList.add("hidden");
      cancelBtn.classList.add("hidden");
    }
  });

  cancelBtn.addEventListener("click", () => {
    isRecording = false;
    hotkeyInput.value = originalHotkey;
    saveBtn.classList.add("hidden");
    cancelBtn.classList.add("hidden");
  });
});
