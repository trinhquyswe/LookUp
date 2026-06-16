use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Meaning {
    pub pos: String,
    pub definition: String,
    pub example: Option<String>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct LookupResponse {
    pub word: String,
    pub pronunciation: Option<String>,
    pub audio: Option<String>,
    pub vi_meanings: Vec<Meaning>,
    pub en_meanings: Vec<Meaning>,
}

pub trait DictionaryProvider {
    fn lookup(&self, word: &str) -> Result<RawLookupResult, String>;
}

pub struct RawLookupResult {
    pub pronunciation: Option<String>,
    pub audio: Option<String>,
    pub meanings: Vec<Meaning>,
}

// Concrete VI Provider
pub struct ViDictionaryProvider {
    client: reqwest::blocking::Client,
}

impl ViDictionaryProvider {
    pub fn new(timeout_sec: u64) -> Self {
        let client = reqwest::blocking::Client::builder()
            .timeout(std::time::Duration::from_secs(timeout_sec))
            .build()
            .unwrap();
        Self { client }
    }
}

#[derive(Deserialize, Debug)]
struct ViMeaningsNode {
    definition: String,
    pos: Option<String>,
    example: Option<String>,
}

#[derive(Deserialize, Debug)]
struct ViPronunciationNode {
    ipa: Option<String>,
}

#[derive(Deserialize, Debug)]
struct ViResultNode {
    meanings: Option<Vec<ViMeaningsNode>>,
    pronunciations: Option<Vec<ViPronunciationNode>>,
}

#[derive(Deserialize, Debug)]
struct ViApiResponse {
    exists: bool,
    results: Option<Vec<ViResultNode>>,
}

impl DictionaryProvider for ViDictionaryProvider {
    fn lookup(&self, word: &str) -> Result<RawLookupResult, String> {
        let mut url = reqwest::Url::parse("https://dict.minhqnd.com/api/v1/lookup")
            .map_err(|e| e.to_string())?;
        url.query_pairs_mut()
            .append_pair("word", word)
            .append_pair("lang", "en")
            .append_pair("def_lang", "vi");

        let response = self.client.get(url).send().map_err(|e| e.to_string())?;
        let api_res: ViApiResponse = response.json().map_err(|e| e.to_string())?;

        if !api_res.exists {
            return Ok(RawLookupResult {
                pronunciation: None,
                audio: None,
                meanings: Vec::new(),
            });
        }

        let mut pronunciation = None;
        let mut meanings = Vec::new();

        if let Some(results) = api_res.results {
            for res in results {
                if let Some(ref prons) = res.pronunciations {
                    for pron in prons {
                        if let Some(ref ipa) = pron.ipa {
                            if pronunciation.is_none() {
                                pronunciation = Some(ipa.clone());
                            }
                        }
                    }
                }
                if let Some(ref ms) = res.meanings {
                    for m in ms {
                        meanings.push(Meaning {
                            pos: m.pos.clone().unwrap_or_else(|| "Thán từ".to_string()),
                            definition: m.definition.clone(),
                            example: m.example.clone(),
                        });
                    }
                }
            }
        }

        Ok(RawLookupResult {
            pronunciation,
            audio: None,
            meanings,
        })
    }
}

// Concrete EN Provider
pub struct EnDictionaryProvider {
    client: reqwest::blocking::Client,
}

impl EnDictionaryProvider {
    pub fn new(timeout_sec: u64) -> Self {
        let client = reqwest::blocking::Client::builder()
            .timeout(std::time::Duration::from_secs(timeout_sec))
            .build()
            .unwrap();
        Self { client }
    }
}

#[derive(Deserialize, Debug)]
struct EnPhoneticNode {
    text: Option<String>,
    audio: Option<String>,
}

#[derive(Deserialize, Debug)]
struct EnDefinitionNode {
    definition: String,
    example: Option<String>,
}

#[derive(Deserialize, Debug)]
struct EnMeaningNode {
    #[serde(rename = "partOfSpeech")]
    part_of_speech: Option<String>,
    definitions: Option<Vec<EnDefinitionNode>>,
}

#[derive(Deserialize, Debug)]
struct EnApiEntry {
    #[serde(default)]
    phonetics: Option<Vec<EnPhoneticNode>>,
    #[serde(default)]
    meanings: Option<Vec<EnMeaningNode>>,
}

impl DictionaryProvider for EnDictionaryProvider {
    fn lookup(&self, word: &str) -> Result<RawLookupResult, String> {
        let word_encoded = match reqwest::Url::parse(&format!("http://x/{}", word)) {
            Ok(u) => u.path()[1..].to_string(),
            Err(_) => word.to_string(),
        };
        let url_str = format!("https://api.dictionaryapi.dev/api/v2/entries/en/{}", word_encoded);

        let response = self.client.get(&url_str).send().map_err(|e| e.to_string())?;
        let entries: Vec<EnApiEntry> = response.json().map_err(|e| e.to_string())?;

        let mut pronunciation = None;
        let mut audio = None;
        let mut meanings = Vec::new();

        for entry in entries {
            if let Some(ref phons) = entry.phonetics {
                for phon in phons {
                    if pronunciation.is_none() && phon.text.is_some() {
                        pronunciation = phon.text.clone();
                    }
                    if audio.is_none() && phon.audio.is_some() {
                        let aud = phon.audio.clone().unwrap();
                        if !aud.is_empty() {
                            audio = Some(aud);
                        }
                    }
                }
            }
            if let Some(ref ms) = entry.meanings {
                for m in ms {
                    let pos = m.part_of_speech.clone().unwrap_or_else(|| "definition".to_string());
                    if let Some(ref defs) = m.definitions {
                        for def in defs {
                            meanings.push(Meaning {
                                pos: pos.clone(),
                                definition: def.definition.clone(),
                                example: def.example.clone(),
                            });
                        }
                    }
                }
            }
        }

        Ok(RawLookupResult {
            pronunciation,
            audio,
            meanings,
        })
    }
}

// Coordinator (Composite/Service Orchestrator)
pub struct DictionaryService {
    vi_provider: ViDictionaryProvider,
    en_provider: EnDictionaryProvider,
}

impl DictionaryService {
    pub fn new() -> Self {
        Self {
            vi_provider: ViDictionaryProvider::new(3),
            en_provider: EnDictionaryProvider::new(3),
        }
    }

    pub fn lookup(&self, word: &str) -> Result<LookupResponse, String> {
        let word_trimmed = word.trim().to_lowercase();
        if word_trimmed.is_empty() {
            return Err("Word is empty".to_string());
        }

        let vi_res = self.vi_provider.lookup(&word_trimmed).unwrap_or_else(|_| RawLookupResult {
            pronunciation: None,
            audio: None,
            meanings: Vec::new(),
        });

        let en_res = self.en_provider.lookup(&word_trimmed).unwrap_or_else(|_| RawLookupResult {
            pronunciation: None,
            audio: None,
            meanings: Vec::new(),
        });

        let pronunciation = vi_res.pronunciation.or(en_res.pronunciation);

        Ok(LookupResponse {
            word: word_trimmed,
            pronunciation,
            audio: en_res.audio,
            vi_meanings: vi_res.meanings,
            en_meanings: en_res.meanings,
        })
    }
}
