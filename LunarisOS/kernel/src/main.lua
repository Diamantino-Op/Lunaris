packed struct limine_base_revision {
    magic0: u64
    magic1: u64
    revision: u64
}

packed struct limine_framebuffer {
    address: u8*
    width: u64
    height: u64
    pitch: u64
}

packed struct limine_framebuffer_response {
    revision: u64
    framebuffer_count: u64
    framebuffers: limine_framebuffer**
}

packed struct limine_framebuffer_request {
    id0: u64
    id1: u64
    id2: u64
    id3: u64
    revision: u64
    response: limine_framebuffer_response*
}

asm function limine_base_revision_ptr(): limine_base_revision* = limine_base_revision_ptr;
asm function limine_framebuffer_request_ptr(): limine_framebuffer_request* = limine_framebuffer_request_ptr;
asm function hlt(): void = hlt;

function hcf()
    while 1 do
        hlt()
    end
end

function kernel_main()
    local base_revision: limine_base_revision* = limine_base_revision_ptr()
    if base_revision.revision != 0 then
        hcf()
    end

    local framebuffer_request: limine_framebuffer_request* = limine_framebuffer_request_ptr()
    if framebuffer_request.response == 0 then
        hcf()
    end

    if framebuffer_request.response.framebuffer_count < 1 then
        hcf()
    end

    local framebuffer: limine_framebuffer* = framebuffer_request.response.framebuffers[0]
    local fb_ptr: u32* = framebuffer.address
    local pitch: u64 = framebuffer.pitch / 4
    local y: u64 = 0

    while y < framebuffer.height do
        local x: u64 = 0
        while x < framebuffer.width do
            local n_x: u64 = x * 255 / framebuffer.width
            local n_y: u64 = y * 255 / framebuffer.height
            fb_ptr[y * pitch + x] = n_y * 256 + n_x
            x = x + 1
        end
        y = y + 1
    end

    hcf()
end