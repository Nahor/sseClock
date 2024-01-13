use embed_resource;

fn main() {
    println!("cargo:rerun-if-changed=sse_clock.rc");
    println!("cargo:rerun-if-changed=resources/sse_clock.ico");
    embed_resource::compile("sse_clock.rc", embed_resource::NONE);
}
