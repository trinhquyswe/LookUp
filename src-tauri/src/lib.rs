use mouse_position::mouse_position::Mouse;
use ocrs::{ImageSource, OcrEngine, OcrEngineParams};
use rten::Model;
use rten_imageproc::{BoundingRect, RotatedRect};
use screenshots::Screen;
use std::path::PathBuf;
use std::sync::Mutex;
use tauri::{Emitter, Manager};

// ── Persistent engine state ───────────────────────────────────────────────────

pub struct OcrState(pub Mutex<Option<OcrEngine>>);

// ── Model helpers ─────────────────────────────────────────────────────────────

fn models_dir() -> PathBuf {
    let exe = std::env::current_exe().unwrap();
    let exe_dir = exe.parent().unwrap();
    let prod = exe_dir.join("models");
    if prod.exists() {
        return prod;
    }
    exe_dir
        .ancestors()
        .find(|p| p.join("Cargo.toml").exists())
        .unwrap_or(exe_dir)
        .join("models")
}

fn init_engine() -> Result<OcrEngine, String> {
    let dir = models_dir();
    let det_path = dir.join("text-detection.rten");
    let rec_path = dir.join("text-recognition.rten");
    let detection_model = Model::load_file(&det_path)
        .map_err(|e| format!("Detection model not found at {det_path:?}: {e}"))?;
    let recognition_model = Model::load_file(&rec_path)
        .map_err(|e| format!("Recognition model not found at {rec_path:?}: {e}"))?;
    OcrEngine::new(OcrEngineParams {
        detection_model: Some(detection_model),
        recognition_model: Some(recognition_model),
        ..Default::default()
    })
    .map_err(|e| format!("Failed to create OCR engine: {e}"))
}

fn with_engine<T>(
    state: &OcrState,
    f: impl FnOnce(&OcrEngine) -> Result<T, String>,
) -> Result<T, String> {
    let mut guard = state.0.lock().unwrap();
    if guard.is_none() {
        *guard = Some(init_engine()?);
    }
    f(guard.as_ref().unwrap())
}

// ── Mouse helpers ─────────────────────────────────────────────────────────────

fn get_cursor_pos() -> Result<(i32, i32), String> {
    match Mouse::get_mouse_position() {
        Mouse::Position { x, y } => Ok((x as i32, y as i32)),
        Mouse::Error => Err("Failed to read mouse position".into()),
    }
}

// ── Core OCR logic ────────────────────────────────────────────────────────────

fn detect_word_at(engine: &OcrEngine, cursor_x: i32, cursor_y: i32) -> Result<String, String> {
    // Find the screen the cursor is on
    let screens = Screen::all().map_err(|e| format!("Screen enumeration failed: {e}"))?;
    let screen = screens
        .into_iter()
        .find(|s| {
            let d = &s.display_info;
            cursor_x >= d.x
                && cursor_x < d.x + d.width as i32
                && cursor_y >= d.y
                && cursor_y < d.y + d.height as i32
        })
        .ok_or_else(|| format!("No screen at ({cursor_x},{cursor_y})"))?;

    let capture = screen
        .capture()
        .map_err(|e| format!("Capture failed: {e}"))?;

    // Map global cursor → image pixel coordinates (DPI-aware)
    let scale = screen.display_info.scale_factor;
    let img_x = ((cursor_x - screen.display_info.x) as f32 * scale) as i32;
    let img_y = ((cursor_y - screen.display_info.y) as f32 * scale) as i32;

    let source = ImageSource::from_bytes(capture.as_raw(), (capture.width(), capture.height()))
        .map_err(|e| format!("ImageSource error: {e}"))?;
    let input = engine
        .prepare_input(source)
        .map_err(|e| format!("prepare_input failed: {e}"))?;

    let words: Vec<RotatedRect> = engine
        .detect_words(&input)
        .map_err(|e| format!("detect_words failed: {e}"))?;

    const PAD: f32 = 6.0;
    let fx = img_x as f32;
    let fy = img_y as f32;

    let target = words.iter().find(|&rect| {
        let br = rect.bounding_rect();
        fx >= br.left() - PAD
            && fx <= br.right() + PAD
            && fy >= br.top() - PAD
            && fy <= br.bottom() + PAD
    });

    let word_rect = match target {
        Some(r) => r,
        None => return Ok(String::new()),
    };

    // ── Crop to the exact word bounding box ───────────────────────────────────
    // Feeding only the word's own pixels to the model makes it physically
    // impossible for neighbouring text to appear in the result.
    let br = word_rect.bounding_rect();
    const CROP_PAD: i32 = 4;
    let cx1 = (br.left()   as i32 - CROP_PAD).max(0) as u32;
    let cy1 = (br.top()    as i32 - CROP_PAD).max(0) as u32;
    let cx2 = (br.right()  as i32 + CROP_PAD).min(capture.width()  as i32) as u32;
    let cy2 = (br.bottom() as i32 + CROP_PAD).min(capture.height() as i32) as u32;
    let cw = cx2.saturating_sub(cx1).max(1);
    let ch = cy2.saturating_sub(cy1).max(1);

    // Convert RGBA capture -> RGB, then crop to the word rect
    let img_rgba = image::RgbaImage::from_raw(
        capture.width(),
        capture.height(),
        capture.as_raw().to_vec(),
    )
    .ok_or_else(|| "Failed to wrap capture as RgbaImage".to_string())?;
    let img_rgb = image::DynamicImage::ImageRgba8(img_rgba).into_rgb8();
    let cropped = image::imageops::crop_imm(&img_rgb, cx1, cy1, cw, ch).to_image();

    // OCR on the isolated crop
    let crop_src =
        ImageSource::from_bytes(cropped.as_raw(), (cropped.width(), cropped.height()))
            .map_err(|e| format!("Crop ImageSource error: {e}"))?;
    let crop_in = engine
        .prepare_input(crop_src)
        .map_err(|e| format!("Crop prepare_input failed: {e}"))?;
    let raw = engine
        .get_text(&crop_in)
        .map_err(|e| format!("Crop get_text failed: {e}"))?;

    // Take only the first whitespace token as an extra safeguard
    Ok(raw.split_whitespace().next().unwrap_or("").to_string())
}

// ── Tauri commands ────────────────────────────────────────────────────────────

#[tauri::command]
fn ocr_from_file(path: String, state: tauri::State<OcrState>) -> Result<String, String> {
    let img = image::open(&path)
        .map_err(|e| format!("Failed to open '{path}': {e}"))?
        .into_rgb8();
    let (w, h) = img.dimensions();
    with_engine(&state, |engine| {
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
fn ocr_screenshot(state: tauri::State<OcrState>) -> Result<String, String> {
    let screens = Screen::all().map_err(|e| format!("Screen enumeration failed: {e}"))?;
    let screen = screens
        .into_iter()
        .next()
        .ok_or_else(|| "No screen found".to_string())?;
    let capture = screen
        .capture()
        .map_err(|e| format!("Capture failed: {e}"))?;
    with_engine(&state, |engine| {
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
fn ocr_word_at_cursor(state: tauri::State<OcrState>) -> Result<String, String> {
    let (cx, cy) = get_cursor_pos()?;
    with_engine(&state, |engine| detect_word_at(engine, cx, cy))
}

// ── Popup positioner ──────────────────────────────────────────────────────────

/// Show the popup window near the cursor with the detected word.
fn show_popup(app: &tauri::AppHandle, word: String, cursor_x: i32, cursor_y: i32) {
    let Some(popup) = app.get_webview_window("popup") else {
        return;
    };

    // Position the popup above and centred on the cursor.
    // popup is 320×160 px
    let px = (cursor_x - 160).max(8);
    let py = (cursor_y - 190).max(8);

    let _ = popup.set_position(tauri::PhysicalPosition::new(px, py));
    let _ = popup.emit("show-word", &word);
    let _ = popup.show();
    let _ = popup.set_focus();
}

// ── App entry point ───────────────────────────────────────────────────────────

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    use tauri::menu::{Menu, MenuItem};
    use tauri::tray::TrayIconBuilder;
    use tauri_plugin_global_shortcut::{Code, GlobalShortcutExt, Modifiers, Shortcut, ShortcutState};

    tauri::Builder::default()
        .manage(OcrState(Mutex::new(None)))
        .plugin(tauri_plugin_opener::init())
        .plugin(
            tauri_plugin_global_shortcut::Builder::new()
                .with_handler(|app, _shortcut, event| {
                    if event.state() == ShortcutState::Pressed {
                        let app = app.clone();
                        std::thread::spawn(move || {
                            // Capture cursor position BEFORE slow OCR starts
                            let (cx, cy) = match get_cursor_pos() {
                                Ok(p) => p,
                                Err(e) => {
                                    eprintln!("Cursor error: {e}");
                                    return;
                                }
                            };

                            let state = app.state::<OcrState>();
                            let word = with_engine(&state, |engine| {
                                detect_word_at(engine, cx, cy)
                            })
                            .unwrap_or_default();

                            show_popup(&app, word, cx, cy);
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
            ocr_from_file,
            ocr_screenshot,
            ocr_word_at_cursor,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
