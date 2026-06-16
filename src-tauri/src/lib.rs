use mouse_position::mouse_position::Mouse;
use ocrs::{ImageSource, OcrEngine, OcrEngineParams};
use rten::Model;
use rten_imageproc::{BoundingRect, RotatedRect};
use screenshots::Screen;
use std::path::PathBuf;
use std::sync::Mutex;
use tauri::{Emitter, Manager};

// ── Persistent engine state (lazy-initialised once, reused on every hotkey) ──

pub struct OcrState(pub Mutex<Option<OcrEngine>>);

// ── Helpers ───────────────────────────────────────────────────────────────────

fn models_dir() -> PathBuf {
    let exe = std::env::current_exe().unwrap();
    let exe_dir = exe.parent().unwrap();

    // Production: resources copied next to .exe
    let prod = exe_dir.join("models");
    if prod.exists() {
        return prod;
    }

    // Development: src-tauri/models/
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

/// Ensure engine is initialised and call `f` with a reference to it.
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

// ── Core logic: capture + detect word under cursor ────────────────────────────

fn detect_word_at_cursor(engine: &OcrEngine) -> Result<String, String> {
    // 1. Get cursor position (global screen coordinates)
    let (cursor_x, cursor_y) = match Mouse::get_mouse_position() {
        Mouse::Position { x, y } => (x as i32, y as i32),
        Mouse::Error => return Err("Failed to read mouse position".into()),
    };

    // 2. Find which screen the cursor is on
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
        .ok_or_else(|| format!("No screen found at cursor ({cursor_x},{cursor_y})"))?;

    // 3. Capture that screen
    let capture = screen
        .capture()
        .map_err(|e| format!("Screen capture failed: {e}"))?;

    // 4. Map cursor → image pixel coordinates (account for DPI scale)
    let scale = screen.display_info.scale_factor;
    let img_x = ((cursor_x - screen.display_info.x) as f32 * scale) as i32;
    let img_y = ((cursor_y - screen.display_info.y) as f32 * scale) as i32;

    // 5. Prepare OCR input
    let source = ImageSource::from_bytes(capture.as_raw(), (capture.width(), capture.height()))
        .map_err(|e| format!("ImageSource error: {e}"))?;
    let input = engine
        .prepare_input(source)
        .map_err(|e| format!("OCR prepare_input failed: {e}"))?;

    // 6. Detect all word bounding boxes
    let words: Vec<RotatedRect> = engine
        .detect_words(&input)
        .map_err(|e| format!("detect_words failed: {e}"))?;

    // 7. Find word whose bounding rect contains the cursor (±4 px padding)
    const PAD: f32 = 4.0;
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
        None => return Ok(String::new()), // cursor not over any word
    };

    // 8. Recognise just that word (pass it as a single-word line)
    let single_line = vec![vec![*word_rect]];
    let recognized = engine
        .recognize_text(&input, &single_line)
        .map_err(|e| format!("recognize_text failed: {e}"))?;

    let word = recognized
        .into_iter()
        .flatten()
        .map(|t| t.to_string())
        .collect::<Vec<_>>()
        .join("")
        .trim()
        .to_string();

    Ok(word)
}

// ── Tauri commands ────────────────────────────────────────────────────────────

/// OCR an image file and return all text.
#[tauri::command]
fn ocr_from_file(path: String, state: tauri::State<OcrState>) -> Result<String, String> {
    let img = image::open(&path)
        .map_err(|e| format!("Failed to open image '{path}': {e}"))?
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

/// OCR the entire primary screen and return all text.
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

/// Detect and return the word currently under the mouse cursor.
#[tauri::command]
fn ocr_word_at_cursor(state: tauri::State<OcrState>) -> Result<String, String> {
    with_engine(&state, |engine| detect_word_at_cursor(engine))
}

// ── App entry point ───────────────────────────────────────────────────────────

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    use tauri_plugin_global_shortcut::{Code, GlobalShortcutExt, Modifiers, Shortcut, ShortcutState};

    tauri::Builder::default()
        .manage(OcrState(Mutex::new(None)))
        .plugin(tauri_plugin_opener::init())
        // Global shortcut: Ctrl + Shift + Space
        .plugin(
            tauri_plugin_global_shortcut::Builder::new()
                .with_handler(|app, _shortcut, event| {
                    if event.state() == ShortcutState::Pressed {
                        let app = app.clone();
                        std::thread::spawn(move || {
                            let state = app.state::<OcrState>();
                            match with_engine(&state, |engine| detect_word_at_cursor(engine)) {
                                Ok(word) => {
                                    let _ = app.emit("word-detected", word);
                                }
                                Err(e) => {
                                    let _ = app.emit("word-error", e);
                                }
                            }
                        });
                    }
                })
                .build(),
        )
        .setup(|app| {
            // Register Ctrl+Shift+Space as the global hotkey
            app.global_shortcut().register(Shortcut::new(
                Some(Modifiers::CONTROL | Modifiers::SHIFT),
                Code::Space,
            ))?;
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            ocr_from_file,
            ocr_screenshot,
            ocr_word_at_cursor,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
