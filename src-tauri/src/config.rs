use serde::{Deserialize, Serialize};
use tauri::Manager;

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct AppConfig {
    pub hotkey: String,
}

pub fn load_config(app: &tauri::AppHandle) -> AppConfig {
    let config_dir = match app.path().app_config_dir() {
        Ok(p) => p,
        Err(_) => return AppConfig { hotkey: "None".to_string() },
    };
    let config_path = config_dir.join("config.json");
    if !config_path.exists() {
        return AppConfig { hotkey: "None".to_string() };
    }
    let content = std::fs::read_to_string(&config_path).unwrap_or_default();
    serde_json::from_str(&content).unwrap_or_else(|_| AppConfig { hotkey: "None".to_string() })
}

pub fn save_config(app: &tauri::AppHandle, config: &AppConfig) -> Result<(), String> {
    let config_dir = app.path().app_config_dir().map_err(|e| e.to_string())?;
    std::fs::create_dir_all(&config_dir).map_err(|e| e.to_string())?;
    let config_path = config_dir.join("config.json");
    let content = serde_json::to_string_pretty(config).map_err(|e| e.to_string())?;
    std::fs::write(&config_path, content).map_err(|e| e.to_string())?;
    Ok(())
}
