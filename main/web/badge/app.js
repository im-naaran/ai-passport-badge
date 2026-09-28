(function (root) {
    "use strict";

    const encoder = new TextEncoder();
    const MAGIC = 0x46525042;
    const VERSION = 2;
    const HEADER_SIZE = 14;
    const NAME_MAX_BYTES = 48;
    const BIO_MAX_BYTES = 96;
    const IMAGE_BYTES = 80000;

    function nameBytes(name) { return encoder.encode(name.trim()); }
    function bioBytes(bio) { return encoder.encode(bio.trim()); }

    function validName(name) {
        const bytes = nameBytes(name);
        return bytes.length > 0 && bytes.length <= NAME_MAX_BYTES &&
            !/[\u0000-\u001f\u007f]/u.test(name);
    }

    function validBio(bio) {
        const bytes = bioBytes(bio || "");
        return bytes.length <= BIO_MAX_BYTES && !/[\u0000-\u001f\u007f]/u.test(bio || "");
    }

    function buildEnvelope(name, bio, image) {
        const bytes = nameBytes(name);
        const bioData = bioBytes(bio || "");
        if (!validName(name)) throw new Error("姓名应为 1–48 个 UTF-8 字节且不能包含控制符");
        if (!validBio(bio)) throw new Error("自定义语句应不超过 96 个 UTF-8 字节且不能包含控制符");
        if (!(image instanceof Uint8Array) || image.length !== IMAGE_BYTES)
            throw new Error("照片数据尺寸不正确");
        const body = new Uint8Array(HEADER_SIZE + bytes.length + bioData.length + image.length);
        const view = new DataView(body.buffer);
        /* This explicit little-endian envelope mirrors the bounded firmware streaming parser. */
        view.setUint32(0, MAGIC, true);
        view.setUint16(4, VERSION, true);
        view.setUint16(6, bytes.length, true);
        view.setUint16(8, bioData.length, true);
        view.setUint32(10, image.length, true);
        body.set(bytes, HEADER_SIZE);
        body.set(bioData, HEADER_SIZE + bytes.length);
        body.set(image, HEADER_SIZE + bytes.length + bioData.length);
        return body;
    }

    function saveDisabled(state) {
        return !state.hasImage || !validName(state.name || "") ||
            !validBio(state.bio || "") || state.uploading;
    }

    function errorMessage(code, status) {
        const messages = {
            invalid_token: "配置会话已失效，请重新进入设备设置页",
            invalid_length: "提交的数据长度不正确",
            invalid_name: "姓名格式不正确",
            invalid_bio: "自定义语句格式不正确",
            busy: "设备正在保存，请稍后再试",
            storage_error: "设备保存失败，原工牌资料仍会保留",
            timeout: "上传超时，请重新保存",
        };
        return messages[code] || (status === 409 ? messages.busy : "保存失败，请重试");
    }

    root.BadgeAppLogic = {
        nameBytes, bioBytes, validName, validBio, buildEnvelope, saveDisabled, errorMessage,
    };

    if (typeof document === "undefined") return;

    const elements = {
        form: document.querySelector("#profile-form"),
        canvas: document.querySelector("#preview"),
        file: document.querySelector("#photo"),
        name: document.querySelector("#name"),
        nameCount: document.querySelector("#name-count"),
        bio: document.querySelector("#bio"),
        bioCount: document.querySelector("#bio-count"),
        zoom: document.querySelector("#zoom"),
        rotate: document.querySelector("#rotate"),
        shapes: Array.from(document.querySelectorAll('input[name="shape"]')),
        save: document.querySelector("#save"),
        connection: document.querySelector("#connection"),
        message: document.querySelector("#message"),
    };
    const context = elements.canvas.getContext("2d", { willReadFrequently: true });
    const state = { name: "", bio: "", shape: "square", image: null,
        transform: { rotation: 0, zoom: 1, panX: 0, panY: 0 },
        hasImage: false, uploading: false, token: "", pointer: null };

    function updateControls() {
        state.name = elements.name.value;
        state.bio = elements.bio.value;
        elements.nameCount.textContent = `${nameBytes(state.name).length}/48 字节`;
        elements.nameCount.classList.toggle("invalid", !validName(state.name));
        elements.bioCount.textContent = `${bioBytes(state.bio).length}/96 字节`;
        elements.bioCount.classList.toggle("invalid", !validBio(state.bio));
        elements.save.disabled = saveDisabled(state);
    }

    function showMessage(text, kind = "") {
        elements.message.textContent = text;
        elements.message.dataset.kind = kind;
    }

    function redraw() {
        if (!state.image) return;
        state.transform = Object.assign(state.transform,
            root.BadgeImage.renderCrop(context, state.image, state.transform, 200, state.shape));
    }

    function pointerPosition(event) {
        const rect = elements.canvas.getBoundingClientRect();
        return { x: event.clientX - rect.left, y: event.clientY - rect.top };
    }

    elements.canvas.addEventListener("pointerdown", (event) => {
        if (!state.image) return;
        elements.canvas.setPointerCapture(event.pointerId);
        /* Pointer deltas use CSS pixels, matching the fixed 200 px preview coordinate space. */
        state.pointer = Object.assign(pointerPosition(event), { panX: state.transform.panX, panY: state.transform.panY });
    });
    elements.canvas.addEventListener("pointermove", (event) => {
        if (!state.pointer) return;
        const position = pointerPosition(event);
        state.transform.panX = state.pointer.panX + position.x - state.pointer.x;
        state.transform.panY = state.pointer.panY + position.y - state.pointer.y;
        redraw();
    });
    ["pointerup", "pointercancel"].forEach((type) => elements.canvas.addEventListener(type, () => {
        state.pointer = null;
    }));

    elements.file.addEventListener("change", async () => {
        const file = elements.file.files[0];
        if (!file) return;
        try {
            const decoded = await root.BadgeImage.decodeFile(file);
            if (state.image && typeof state.image.close === "function") state.image.close();
            state.image = decoded;
            state.transform = { rotation: 0, zoom: 1, panX: 0, panY: 0 };
            state.hasImage = true;
            elements.zoom.value = "1";
            redraw(); updateControls(); showMessage("照片已载入，可拖动、缩放或旋转");
        } catch (error) { showMessage(error.message || "图片无法处理", "error"); }
    });
    elements.zoom.addEventListener("input", () => {
        state.transform.zoom = Number(elements.zoom.value); redraw();
    });
    elements.rotate.addEventListener("click", () => {
        if (!state.image) return;
        state.transform = root.BadgeImage.rotateTransform(state.transform, 1); redraw();
    });
    elements.name.addEventListener("input", updateControls);
    elements.bio.addEventListener("input", updateControls);
    elements.shapes.forEach((input) => input.addEventListener("change", () => {
        if (!input.checked) return;
        state.shape = root.BadgeImage.normalizedShape(input.value);
        redraw();
    }));

    async function loadInitial() {
        try {
            const statusResponse = await fetch("/api/status", { cache: "no-store" });
            if (!statusResponse.ok) throw new Error("无法读取设备状态");
            const status = await statusResponse.json();
            /* The token exists only for this physical hotspot session and is never persisted locally. */
            state.token = String(status.token || "");
            elements.connection.textContent = status.connected ? "手机已连接" : "已连接设备热点";
            const profileResponse = await fetch("/api/profile", { cache: "no-store" });
            if (profileResponse.ok) {
                const profile = await profileResponse.json();
                elements.name.value = profile.name || "";
                elements.bio.value = profile.bio || "";
            }
            const photoResponse = await fetch("/api/photo", { cache: "no-store" });
            if (photoResponse.ok) {
                const bytes = new Uint8Array(await photoResponse.arrayBuffer());
                const source = document.createElement("canvas");
                source.width = source.height = 200;
                source.getContext("2d").putImageData(
                    root.BadgeImage.rgb565ToImageData(bytes, 200, 200), 0, 0);
                /* RGB565 has no original pixels outside an old baked mask; this source is intentionally lossless-only. */
                state.image = source;
                state.hasImage = true;
                redraw();
            }
            updateControls();
        } catch (error) {
            elements.connection.textContent = "设备连接异常";
            showMessage(error.message || "页面初始化失败", "error");
        }
    }

    elements.form.addEventListener("submit", async (event) => {
        event.preventDefault();
        if (saveDisabled(state)) return;
        state.uploading = true; updateControls(); showMessage("正在保存，请勿关闭页面…");
        const rgba = context.getImageData(0, 0, 200, 200).data;
        const body = buildEnvelope(state.name, state.bio,
            root.BadgeImage.rgbaToRgb565(rgba, 200 * 200));
        try {
            const response = await fetch("/api/profile", { method: "POST",
                headers: { "Content-Type": "application/octet-stream", "X-Passport-Session": state.token }, body });
            if (!response.ok) {
                let payload = {};
                try { payload = await response.json(); } catch (_) { /* use stable HTTP fallback */ }
                throw Object.assign(new Error(errorMessage(payload.error, response.status)), { known: true });
            }
            showMessage("已保存，可继续修改或按设备任意键退出", "success");
        } catch (error) {
            showMessage(error.known ? error.message : "连接已中断，保存结果未知，请查看设备工牌", "error");
        } finally {
            state.uploading = false; updateControls();
        }
    });

    updateControls();
    loadInitial();
})(typeof globalThis !== "undefined" ? globalThis : window);
