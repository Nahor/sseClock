use winresource::WindowsResource;

fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").unwrap() == "windows" {
        let mut res = WindowsResource::new();
        res.set_icon_with_id("resources/sse_clock.ico", "exe-icon");
        res.compile().unwrap();
    }
}
