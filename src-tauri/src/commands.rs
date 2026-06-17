use crate::ocr::{self, OcrState};
use crate::dictionary::{DictionaryService, LookupResponse};
use crate::config::{self, AppConfig};
use ocrs::ImageSource;
use screenshots::Screen;
use std::str::FromStr;
use tauri_plugin_global_shortcut::{GlobalShortcutExt, Shortcut};
use tauri::Emitter;

pub struct OcrEnabledState(pub std::sync::atomic::AtomicBool);

#[tauri::command]
pub fn get_ocr_enabled(state: tauri::State<OcrEnabledState>) -> bool {
    state.0.load(std::sync::atomic::Ordering::Relaxed)
}

#[tauri::command]
pub fn toggle_ocr_enabled(
    app: tauri::AppHandle,
    state: tauri::State<OcrEnabledState>,
) -> bool {
    let new_state = !state.0.load(std::sync::atomic::Ordering::Relaxed);
    state.0.store(new_state, std::sync::atomic::Ordering::Relaxed);
    
    // Emit event to notify windows
    let _ = app.emit("ocr-status-changed", new_state);
    new_state
}

pub struct HotkeyState(pub std::sync::Mutex<String>);

#[tauri::command]
pub fn get_hotkey(hotkey_state: tauri::State<HotkeyState>) -> String {
    hotkey_state.0.lock().unwrap().clone()
}

#[tauri::command]
pub fn set_hotkey(
    app: tauri::AppHandle,
    new_hotkey: String,
    hotkey_state: tauri::State<HotkeyState>,
) -> Result<(), String> {
    let global_shortcut = app.global_shortcut();
    let mut guard = hotkey_state.0.lock().unwrap();
    let old_hotkey = guard.clone();

    // Unregister old hotkey if it was registered
    if old_hotkey != "None" && !old_hotkey.is_empty() {
        if let Ok(old_shortcut) = Shortcut::from_str(&old_hotkey) {
            let _ = global_shortcut.unregister(old_shortcut);
        }
    }

    if new_hotkey == "None" || new_hotkey.is_empty() {
        // Just clear the hotkey
        *guard = "None".to_string();
        let config = AppConfig { hotkey: "None".to_string() };
        config::save_config(&app, &config)?;
        return Ok(());
    }

    // Validate 2-key combination: exactly 1 modifier and 1 primary key
    let parts: Vec<&str> = new_hotkey.split('+').collect();
    if parts.len() != 2 {
        return Err("Hotkey must be a combination of exactly 2 keys (e.g., Ctrl+Space)".to_string());
    }
    let modifiers = vec!["Ctrl", "Shift", "Alt", "Super"];
    if !modifiers.contains(&parts[0]) || modifiers.contains(&parts[1]) {
        return Err("Hotkey must be 1 modifier + 1 key (e.g., Ctrl+Space)".to_string());
    }

    // Register new hotkey
    let new_shortcut = Shortcut::from_str(&new_hotkey)
        .map_err(|e| format!("Invalid shortcut format: {e}"))?;

    global_shortcut.register(new_shortcut)
        .map_err(|e| format!("Failed to register new shortcut: {e}. Please ensure it is not already in use."))?;

    // Update state and save config
    *guard = new_hotkey.clone();
    let config = AppConfig { hotkey: new_hotkey };
    config::save_config(&app, &config)?;

    Ok(())
}

#[tauri::command]
pub fn ocr_from_file(path: String, state: tauri::State<OcrState>) -> Result<String, String> {
    let img = image::open(&path)
        .map_err(|e| format!("Failed to open '{path}': {e}"))?
        .into_rgb8();
    let (w, h) = img.dimensions();
    ocr::with_engine(&state, |engine| {
        let source = ImageSource::from_bytes(img.as_raw(), (w, h))
            .map_err(|e| format!("ImageSource error: {e}"))?;
        let input = engine
            .prepare_input(source)
            .map_err(|e| format!("prepare_input failed: {e}"))?;
        engine
            .get_text(&input)
            .map(|t| t.trim().to_string())
            .map_err(|e| format!("get_text failed: {e}"))
    })
}

#[tauri::command]
pub fn ocr_screenshot(state: tauri::State<OcrState>) -> Result<String, String> {
    let screens = Screen::all().map_err(|e| format!("Screen enumeration failed: {e}"))?;
    let screen = screens
        .into_iter()
        .next()
        .ok_or_else(|| "No screen found".to_string())?;
    let capture = screen
        .capture()
        .map_err(|e| format!("Capture failed: {e}"))?;
    ocr::with_engine(&state, |engine| {
        let source =
            ImageSource::from_bytes(capture.as_raw(), (capture.width(), capture.height()))
                .map_err(|e| format!("ImageSource error: {e}"))?;
        let input = engine
            .prepare_input(source)
            .map_err(|e| format!("prepare_input failed: {e}"))?;
        engine
            .get_text(&input)
            .map(|t| t.trim().to_string())
            .map_err(|e| format!("get_text failed: {e}"))
    })
}

#[tauri::command]
pub fn ocr_word_at_cursor(state: tauri::State<OcrState>) -> Result<String, String> {
    let (cx, cy) = ocr::get_cursor_pos()?;
    ocr::with_engine(&state, |engine| ocr::detect_word_at(engine, cx, cy))
}

#[tauri::command]
pub fn lookup_word(
    word: String,
    dict_service: tauri::State<DictionaryService>,
) -> Result<LookupResponse, String> {
    dict_service.lookup(&word)
}
