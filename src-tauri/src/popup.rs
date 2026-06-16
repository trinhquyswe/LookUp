use tauri::{AppHandle, Manager, Emitter};

/// Show the popup window near the cursor with the detected word.
pub fn show_popup(app: &AppHandle, word: String, cursor_x: i32, cursor_y: i32) {
    let Some(popup) = app.get_webview_window("popup") else {
        return;
    };

    // Position the popup above and centred on the cursor.
    // popup is 360×260 px
    let px = (cursor_x - 180).max(8);
    let py = (cursor_y - 290).max(8);

    let _ = popup.set_position(tauri::PhysicalPosition::new(px, py));
    let _ = popup.emit("show-word", &word);
    let _ = popup.show();
    let _ = popup.set_focus();
}
