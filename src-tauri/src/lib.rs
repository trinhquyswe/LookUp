pub mod ocr;
pub mod dictionary;
pub mod popup;
pub mod commands;

use tauri::Manager;

// ── App entry point ───────────────────────────────────────────────────────────

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    use tauri::menu::{Menu, MenuItem};
    use tauri::tray::TrayIconBuilder;
    use tauri_plugin_global_shortcut::{Code, GlobalShortcutExt, Modifiers, Shortcut, ShortcutState};

    tauri::Builder::default()
        .manage(ocr::OcrState(std::sync::Mutex::new(None)))
        .manage(dictionary::DictionaryService::new())
        .plugin(tauri_plugin_opener::init())
        .plugin(
            tauri_plugin_global_shortcut::Builder::new()
                .with_handler(|app, _shortcut, event| {
                    if event.state() == ShortcutState::Pressed {
                        let app = app.clone();
                        std::thread::spawn(move || {
                            // Capture cursor position BEFORE slow OCR starts
                            let (cx, cy) = match ocr::get_cursor_pos() {
                                Ok(p) => p,
                                Err(e) => {
                                    eprintln!("Cursor error: {e}");
                                    return;
                                }
                            };

                            let state = app.state::<ocr::OcrState>();
                            let word = ocr::with_engine(&state, |engine| {
                                ocr::detect_word_at(engine, cx, cy)
                            })
                            .unwrap_or_default();

                            popup::show_popup(&app, word, cx, cy);
                        });
                    }
                })
                .build(),
        )
        .setup(|app| {
            // ── Register global hotkey ─────────────────────────────────────
            app.global_shortcut().register(Shortcut::new(
                Some(Modifiers::CONTROL | Modifiers::SHIFT),
                Code::Space,
            ))?;

            // ── System tray ────────────────────────────────────────────────
            let show_item =
                MenuItem::with_id(app, "show", "Open LookUp", true, None::<&str>)?;
            let quit_item =
                MenuItem::with_id(app, "quit", "Quit LookUp", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&show_item, &quit_item])?;

            TrayIconBuilder::new()
                .icon(app.default_window_icon().unwrap().clone())
                .tooltip("LookUp — Ctrl+Shift+Space to detect word")
                .menu(&menu)
                .on_menu_event(|app, event| match event.id.as_ref() {
                    "show" => {
                        if let Some(w) = app.get_webview_window("main") {
                            let _ = w.show();
                            let _ = w.set_focus();
                        }
                    }
                    "quit" => app.exit(0),
                    _ => {}
                })
                .build(app)?;

            Ok(())
        })
        // Close main window → hide instead of quit
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                if window.label() == "main" {
                    api.prevent_close();
                    let _ = window.hide();
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            commands::ocr_from_file,
            commands::ocr_screenshot,
            commands::ocr_word_at_cursor,
            commands::lookup_word,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
