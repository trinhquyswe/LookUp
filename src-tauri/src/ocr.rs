use std::path::PathBuf;
use std::sync::Mutex;
use ocrs::{ImageSource, OcrEngine, OcrEngineParams};
use rten::Model;
use rten_imageproc::{BoundingRect, RotatedRect};
use screenshots::Screen;
use mouse_position::mouse_position::Mouse;

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

pub fn with_engine<T>(
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
pub fn get_cursor_pos() -> Result<(i32, i32), String> {
    match Mouse::get_mouse_position() {
        Mouse::Position { x, y } => Ok((x as i32, y as i32)),
        Mouse::Error => Err("Failed to read mouse position".into()),
    }
}

// ── Core OCR logic ────────────────────────────────────────────────────────────
pub fn detect_word_at(engine: &OcrEngine, cursor_x: i32, cursor_y: i32) -> Result<String, String> {
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

    // Crop to the exact word bounding box
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

    // Take only the first whitespace token as a safeguard
    Ok(raw.split_whitespace().next().unwrap_or("").to_string())
}
