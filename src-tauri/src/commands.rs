use crate::ocr::{self, OcrState};
use crate::dictionary::{DictionaryService, LookupResponse};
use crate::config::{self, AppConfig};
use ocrs::ImageSource;
use screenshots::Screen;
use std::str::FromStr;
use tauri_plugin_global_shortcut::{GlobalShortcutExt, Shortcut};

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
    let new_shortcut = Shortcut::from_str(&new_hotkey)
        .map_err(|e| format!("Invalid shortcut format: {e}"))?;

    let mut guard = hotkey_state.0.lock().unwrap();
    let old_hotkey = guard.clone();

    let global_shortcut = app.global_shortcut();

    // Unregister old hotkey if it was registered
    if let Ok(old_shortcut) = Shortcut::from_str(&old_hotkey) {
        let _ = global_shortcut.unregister(old_shortcut);
    }

    // Register new hotkey
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
