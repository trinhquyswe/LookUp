use ocrs::{ImageSource, OcrEngine, OcrEngineParams};
use rten::Model;
use std::path::PathBuf;

// ── Helper: resolve path to bundled models ────────────────────────────────────

fn models_dir() -> PathBuf {
    // Dev: models/ lives next to the cargo workspace (src-tauri/models/)
    // Prod: resources are copied next to the .exe
    let exe = std::env::current_exe().unwrap();
    let exe_dir = exe.parent().unwrap();

    // Try next to exe first (production bundle)
    let prod = exe_dir.join("models");
    if prod.exists() {
        return prod;
    }

    // Fall back to dev layout: src-tauri/models/
    let dev = exe_dir
        .ancestors()
        .find(|p| p.join("Cargo.toml").exists())
        .unwrap_or(exe_dir)
        .join("models");
    dev
}

fn load_engine() -> Result<OcrEngine, String> {
    let dir = models_dir();
    let det_path = dir.join("text-detection.rten");
    let rec_path = dir.join("text-recognition.rten");

    let detection_model = Model::load_file(&det_path)
        .map_err(|e| format!("Failed to load detection model at {:?}: {e}", det_path))?;
    let recognition_model = Model::load_file(&rec_path)
        .map_err(|e| format!("Failed to load recognition model at {:?}: {e}", rec_path))?;

    OcrEngine::new(OcrEngineParams {
        detection_model: Some(detection_model),
        recognition_model: Some(recognition_model),
        ..Default::default()
    })
    .map_err(|e| format!("Failed to create OCR engine: {e}"))
}

// ── Command: OCR from an image file path ─────────────────────────────────────

#[tauri::command]
fn ocr_from_file(path: String) -> Result<String, String> {
    let engine = load_engine()?;

    let img = image::open(&path)
        .map_err(|e| format!("Failed to open image '{path}': {e}"))?
        .into_rgb8();

    let (w, h) = img.dimensions();
    let source = ImageSource::from_bytes(img.as_raw(), (w, h))
        .map_err(|e| format!("Failed to prepare image source: {e}"))?;

    let ocr_input = engine
        .prepare_input(source)
        .map_err(|e| format!("OCR prepare failed: {e}"))?;

    let text = engine
        .get_text(&ocr_input)
        .map_err(|e| format!("OCR text extraction failed: {e}"))?;

    Ok(text.trim().to_string())
}

// ── Command: OCR from a live screenshot ──────────────────────────────────────

#[tauri::command]
fn ocr_screenshot() -> Result<String, String> {
    use screenshots::Screen;

    let screens = Screen::all().map_err(|e| format!("Failed to enumerate screens: {e}"))?;
    let screen = screens
        .into_iter()
        .next()
        .ok_or_else(|| "No screen found".to_string())?;

    let capture = screen
        .capture()
        .map_err(|e| format!("Screen capture failed: {e}"))?;

    let engine = load_engine()?;

    let source = ImageSource::from_bytes(capture.as_raw(), (capture.width(), capture.height()))
        .map_err(|e| format!("Failed to prepare screenshot source: {e}"))?;

    let ocr_input = engine
        .prepare_input(source)
        .map_err(|e| format!("OCR prepare failed: {e}"))?;

    let text = engine
        .get_text(&ocr_input)
        .map_err(|e| format!("OCR text extraction failed: {e}"))?;

    Ok(text.trim().to_string())
}

// ── App entry point ───────────────────────────────────────────────────────────

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![ocr_from_file, ocr_screenshot])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
