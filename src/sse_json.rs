use serde::Serialize;

#[derive(Debug, Clone, Serialize)]
pub struct SSEGameMetadataRequest<'a> {
    pub game: &'a str,
    pub game_display_name: &'a str,
    pub icon_color_id: i32,
}

#[derive(Debug, Clone, Serialize)]
pub struct SSEGameEventRequest<'a> {
    pub game: &'a str,
    pub event: &'a str,
    pub data: SSEEventData<'a>,
}

#[derive(Debug, Clone, Serialize)]
pub struct SSEEventData<'a> {
    pub value: &'a str,
    pub frame: SSEEventFrame<'a>,
}

#[derive(Debug, Clone, Serialize)]
pub struct SSEEventFrame<'a> {
    pub date: &'a str,
    pub time: &'a str,
}

#[derive(Debug, Clone, Serialize)]
pub struct SSERemoveGameRequest<'a> {
    pub game: &'a str,
}

#[derive(Debug, Clone, Serialize)]
pub struct SSEBindEventRequest<'a> {
    pub game: &'a str,
    pub event: &'a str,
    pub handlers: Vec<SSEEventHandler<'a>>,
}

#[derive(Debug, Clone, Serialize)]
pub struct SSEEventHandler<'a> {
    #[serde(rename = "device-type")]
    pub device_type: &'a str,
    pub zone: &'a str,
    pub mode: &'a str,
    pub datas: Vec<SSEFrameData<'a>>,
}

#[derive(Debug, Clone, Serialize)]
pub struct SSEFrameData<'a> {
    #[serde(rename = "icon-id")]
    pub icon_id: i32,

    pub lines: Vec<SSELineData<'a>>,
}

#[derive(Debug, Clone, Serialize)]
pub struct SSELineData<'a> {
    #[serde(rename = "has-text")]
    pub has_text: bool,
    #[serde(rename = "context-frame-key")]
    pub context_frame_key: &'a str,
}
