(function (root) {
    "use strict";

    const encoder = new TextEncoder();
    const MAGIC = 0x46525042;
    const VERSION = 3;
    const HEADER_SIZE = 16;
    const NAME_MAX_BYTES = 48;
    const BIO_MAX_BYTES = 96;
    const IMAGE_BYTES = 80000;
    const CUSTOM_WIDTH = 240;
    const CUSTOM_HEIGHT = 320;
    const CUSTOM_IMAGE_BYTES = CUSTOM_WIDTH * CUSTOM_HEIGHT * 2;
    const SHAPE_VALUES = Object.freeze({ square: 0, rounded: 1, circle: 2 });

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

    function normalizeProfileShape(shape) {
        if (!Object.prototype.hasOwnProperty.call(SHAPE_VALUES, shape))
            throw new Error("工牌照片形状不兼容");
        return shape;
    }

    function shapeExpansionBlocked(sourceShape, targetShape) {
        const target = normalizeProfileShape(targetShape);
        if (sourceShape === null) return false;
        const source = normalizeProfileShape(sourceShape);
        return SHAPE_VALUES[target] < SHAPE_VALUES[source];
    }

    function buildEnvelope(name, bio, image, shape) {
        const bytes = nameBytes(name);
        const bioData = bioBytes(bio || "");
        const normalizedShape = normalizeProfileShape(shape);
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
        view.setUint8(10, SHAPE_VALUES[normalizedShape]);
        view.setUint8(11, 0);
        view.setUint32(12, image.length, true);
        body.set(bytes, HEADER_SIZE);
        body.set(bioData, HEADER_SIZE + bytes.length);
        body.set(image, HEADER_SIZE + bytes.length + bioData.length);
        return body;
    }

    function saveDisabled(state) {
        return !state.hasImage || !validName(state.name || "") ||
            !validBio(state.bio || "") || state.uploading ||
            !Object.prototype.hasOwnProperty.call(SHAPE_VALUES, state.shape) ||
            shapeExpansionBlocked(state.sourceShape, state.shape);
    }

    function errorMessage(code, status) {
        const messages = {
            invalid_token: "配置会话已失效，请重新进入设备设置页",
            invalid_length: "提交的数据长度不正确",
            invalid_name: "姓名格式不正确",
            invalid_bio: "自定义语句格式不正确",
            invalid_shape: "照片形状无效，请重新选择",
            busy: "设备正在保存，请稍后再试",
            storage_error: "设备保存失败，原工牌资料仍会保留",
            timeout: "上传超时，请重新保存",
        };
        return messages[code] || (status === 409 ? messages.busy : "保存失败，请重试");
    }

    function validSlot(slot) { return Number.isInteger(slot) && slot >= 1 && slot <= 3; }

    function normalizePersonalizationMetadata(payload) {
        if (!payload || payload.slotCount !== 3 || payload.width !== CUSTOM_WIDTH ||
            payload.height !== CUSTOM_HEIGHT || payload.imageBytes !== CUSTOM_IMAGE_BYTES ||
            !Array.isArray(payload.slots)) throw new Error("设备返回的个性化配置不兼容");
        const slots = [false, false, false];
        const seen = new Set();
        for (const entry of payload.slots) {
            if (!entry || !validSlot(entry.slot) || typeof entry.occupied !== "boolean" ||
                seen.has(entry.slot))
                throw new Error("设备返回的槽位状态无效");
            seen.add(entry.slot);
            slots[entry.slot - 1] = entry.occupied;
        }
        if (seen.size !== 3) throw new Error("设备返回的槽位状态无效");
        return slots;
    }

    function updateSlotOccupation(slots, slot, occupied) {
        if (!Array.isArray(slots) || slots.length !== 3 || !validSlot(slot))
            throw new RangeError("invalid slot state");
        const next = slots.slice();
        next[slot - 1] = Boolean(occupied);
        return next;
    }

    function personalizationSaveDisabled(state) {
        return !state.hasImage || !validSlot(state.slot) || !state.token ||
            state.mutating || state.loading;
    }

    function personalizationRequest(method, slot, token, body) {
        if (!validSlot(slot)) throw new RangeError("invalid slot");
        if (!token) throw new Error("invalid token");
        const upper = String(method).toUpperCase();
        const headers = { "X-Passport-Session": String(token) };
        const options = { method: upper, headers };
        if (upper === "POST") {
            if (!(body instanceof Uint8Array) || body.length !== CUSTOM_IMAGE_BYTES)
                throw new RangeError("invalid personalization image");
            headers["Content-Type"] = "application/octet-stream";
            options.body = body;
        } else if (upper !== "DELETE") {
            throw new RangeError("invalid personalization method");
        }
        return { path: `/api/personalization/slot/${slot}`, options };
    }

    function personalizationErrorMessage(code, status, clearing = false) {
        const messages = {
            invalid_token: "配置会话已失效，请重新进入设备设置页",
            invalid_slot: "目标槽位无效，请重新选择",
            invalid_length: "图片数据长度不正确",
            invalid_content_type: "图片格式不正确",
            slot_empty: "该槽位已经为空",
            storage_unavailable: "个性化存储暂不可用",
            busy: "设备正在保存，请稍后再试",
            timeout: "上传超时，请重试",
            storage_error: clearing ? "清空失败，原图片仍保留" : "保存失败，原图片仍保留",
        };
        return messages[code] || (status === 409 ? messages.busy :
            (clearing ? "清空失败，原图片仍保留" : "保存失败，请重试"));
    }

    function confirmSlotClear(slot, occupied, confirmFn) {
        return validSlot(slot) && occupied && typeof confirmFn === "function" &&
            confirmFn(`确定清空槽位 ${slot} 吗？此操作只影响该槽位。`);
    }

    root.BadgeAppLogic = {
        nameBytes, bioBytes, validName, validBio, normalizeProfileShape,
        shapeExpansionBlocked, buildEnvelope, saveDisabled, errorMessage,
        CUSTOM_WIDTH, CUSTOM_HEIGHT, CUSTOM_IMAGE_BYTES, validSlot,
        normalizePersonalizationMetadata, updateSlotOccupation,
        personalizationSaveDisabled, personalizationRequest, personalizationErrorMessage,
        confirmSlotClear,
    };

    if (typeof document === "undefined") return;

    const elements = {
        profileTab: document.querySelector("#profile-tab"),
        customTab: document.querySelector("#personalization-tab"),
        profileForm: document.querySelector("#profile-form"),
        profileCanvas: document.querySelector("#preview"),
        profileFile: document.querySelector("#photo"),
        name: document.querySelector("#name"),
        nameCount: document.querySelector("#name-count"),
        bio: document.querySelector("#bio"),
        bioCount: document.querySelector("#bio-count"),
        profileZoom: document.querySelector("#zoom"),
        profileRotate: document.querySelector("#rotate"),
        shapes: Array.from(document.querySelectorAll('input[name="shape"]')),
        profileSave: document.querySelector("#save"),
        profileMessage: document.querySelector("#message"),
        connection: document.querySelector("#connection"),
        customForm: document.querySelector("#personalization-form"),
        customCanvas: document.querySelector("#personalization-preview"),
        customFile: document.querySelector("#personalization-photo"),
        customZoom: document.querySelector("#personalization-zoom"),
        customRotate: document.querySelector("#personalization-rotate"),
        customSave: document.querySelector("#personalization-save"),
        customClear: document.querySelector("#personalization-clear"),
        customMessage: document.querySelector("#personalization-message"),
        slots: Array.from(document.querySelectorAll("[data-slot]")),
    };
    const profileContext = elements.profileCanvas.getContext("2d", { willReadFrequently: true });
    const customContext = elements.customCanvas.getContext("2d", { willReadFrequently: true });
    const session = { token: "", activeTab: "profile" };
    /* Each editor owns its source, transform, dirty flag and feedback so requests cannot cross forms. */
    const profile = {
        name: "", bio: "", shape: "square", sourceShape: null, image: null,
        transform: { rotation: 0, zoom: 1, panX: 0, panY: 0 },
        hasImage: false, uploading: false, dirty: false, needsReload: false, pointer: null,
    };
    const custom = {
        slot: 1, slots: [false, false, false], image: null,
        transform: { rotation: 0, zoom: 1, panX: 0, panY: 0 },
        hasImage: false, dirty: false, mutating: false, loading: false,
        loadedSlot: 0, loadGeneration: 0, needsReload: false, pointer: null,
    };

    function mutationActive() { return profile.uploading || custom.mutating; }

    function showMessage(target, text, kind = "") {
        target.textContent = text;
        target.dataset.kind = kind;
    }

    function closeImage(image) {
        /* ImageBitmap retains decoded pixels; release the previous slot before loading another. */
        if (image && typeof image.close === "function") image.close();
    }

    function resetTransform(state, zoom) {
        state.transform = { rotation: 0, zoom: 1, panX: 0, panY: 0 };
        zoom.value = "1";
    }

    function syncControls() {
        /* A mutation captures its target up front, so every control that could retarget it stays locked. */
        const locked = mutationActive();
        profile.name = elements.name.value;
        profile.bio = elements.bio.value;
        elements.nameCount.textContent = `${nameBytes(profile.name).length}/48 字节`;
        elements.nameCount.classList.toggle("invalid", !validName(profile.name));
        elements.bioCount.textContent = `${bioBytes(profile.bio).length}/96 字节`;
        elements.bioCount.classList.toggle("invalid", !validBio(profile.bio));
        elements.profileSave.disabled = saveDisabled(Object.assign({}, profile, { uploading: locked }));
        elements.profileFile.disabled = locked;
        elements.name.disabled = locked;
        elements.bio.disabled = locked;
        elements.profileZoom.disabled = locked;
        elements.profileRotate.disabled = locked;
        elements.shapes.forEach((input) => { input.disabled = locked; });

        const customState = { hasImage: custom.hasImage, slot: custom.slot,
            token: session.token, mutating: locked, loading: custom.loading };
        elements.customSave.disabled = personalizationSaveDisabled(customState);
        elements.customSave.textContent = `保存到槽位 ${custom.slot}`;
        elements.customClear.disabled = locked || custom.loading || !session.token ||
            !custom.slots[custom.slot - 1];
        elements.customClear.textContent = `清空槽位 ${custom.slot}`;
        elements.profileTab.disabled = locked;
        elements.customTab.disabled = locked;
        elements.slots.forEach((button, index) => {
            const slot = index + 1;
            const active = slot === custom.slot;
            button.disabled = locked || custom.loading;
            button.classList.toggle("active", active);
            button.setAttribute("aria-pressed", String(active));
            button.textContent = `槽位 ${slot} · ${custom.slots[index] ? "已保存" : "空"}`;
        });
        elements.customFile.disabled = locked || custom.loading;
        elements.customZoom.disabled = locked || custom.loading || !custom.hasImage;
        elements.customRotate.disabled = locked || custom.loading || !custom.hasImage;
    }

    function redrawProfile() {
        if (!profile.image) return;
        profile.transform = Object.assign(profile.transform,
            root.BadgeImage.renderCrop(profileContext, profile.image, profile.transform,
                200, 200, profile.shape));
    }

    function redrawCustom() {
        if (!custom.image) return;
        custom.transform = Object.assign(custom.transform,
            root.BadgeImage.renderCrop(customContext, custom.image, custom.transform,
                CUSTOM_WIDTH, CUSTOM_HEIGHT, "square"));
    }

    function attachPan(canvas, state, redraw, markDirty) {
        canvas.addEventListener("pointerdown", (event) => {
            if (!state.image || mutationActive()) return;
            canvas.setPointerCapture(event.pointerId);
            state.pointer = { clientX: event.clientX, clientY: event.clientY,
                panX: state.transform.panX, panY: state.transform.panY };
        });
        canvas.addEventListener("pointermove", (event) => {
            if (!state.pointer) return;
            const rect = canvas.getBoundingClientRect();
            const delta = root.BadgeImage.canvasPointerDelta(
                event.clientX - state.pointer.clientX, event.clientY - state.pointer.clientY,
                canvas.width, canvas.height, rect.width, rect.height);
            state.transform.panX = state.pointer.panX + delta.x;
            state.transform.panY = state.pointer.panY + delta.y;
            redraw();
            markDirty();
        });
        ["pointerup", "pointercancel"].forEach((type) => canvas.addEventListener(type, () => {
            state.pointer = null;
        }));
    }

    attachPan(elements.profileCanvas, profile, redrawProfile, () => { profile.dirty = true; });
    attachPan(elements.customCanvas, custom, redrawCustom, () => { custom.dirty = true; });

    elements.profileFile.addEventListener("change", async () => {
        const file = elements.profileFile.files[0];
        if (!file) return;
        try {
            const decoded = await root.BadgeImage.decodeFile(file);
            closeImage(profile.image);
            profile.image = decoded;
            profile.sourceShape = null;
            resetTransform(profile, elements.profileZoom);
            profile.hasImage = true;
            profile.dirty = true;
            redrawProfile();
            syncControls();
            showMessage(elements.profileMessage, "照片已载入，可拖动、缩放或旋转");
        } catch (error) {
            showMessage(elements.profileMessage, error.message || "图片无法处理", "error");
        }
    });
    elements.profileZoom.addEventListener("input", () => {
        profile.transform.zoom = Number(elements.profileZoom.value);
        profile.dirty = true;
        redrawProfile();
    });
    elements.profileRotate.addEventListener("click", () => {
        if (!profile.image) return;
        profile.transform = root.BadgeImage.rotateTransform(profile.transform, 1);
        profile.dirty = true;
        redrawProfile();
    });
    elements.name.addEventListener("input", () => { profile.dirty = true; syncControls(); });
    elements.bio.addEventListener("input", () => { profile.dirty = true; syncControls(); });
    elements.shapes.forEach((input) => input.addEventListener("change", () => {
        if (!input.checked) return;
        profile.shape = normalizeProfileShape(input.value);
        profile.dirty = true;
        redrawProfile();
        syncControls();
        if (shapeExpansionBlocked(profile.sourceShape, profile.shape)) {
            showMessage(elements.profileMessage,
                "当前设备照片的角落已在上次保存时丢失，请重新选择原图后再保存", "error");
        } else {
            showMessage(elements.profileMessage, "照片形状已更新，可继续调整或保存");
        }
    }));

    function clearCustomEditor() {
        closeImage(custom.image);
        custom.image = null;
        custom.hasImage = false;
        custom.dirty = false;
        custom.pointer = null;
        resetTransform(custom, elements.customZoom);
        customContext.clearRect(0, 0, CUSTOM_WIDTH, CUSTOM_HEIGHT);
    }

    async function responseError(response, clearing = false, profileRequest = false) {
        let payload = {};
        try { payload = await response.json(); } catch (_) { /* use stable HTTP fallback */ }
        return profileRequest ? errorMessage(payload.error, response.status) :
            personalizationErrorMessage(payload.error, response.status, clearing);
    }

    async function loadCustomSlot(slot) {
        const generation = ++custom.loadGeneration;
        custom.loading = true;
        custom.loadedSlot = slot;
        clearCustomEditor();
        syncControls();
        if (!custom.slots[slot - 1]) {
            showMessage(elements.customMessage, `槽位 ${slot} 为空，请选择图片`);
            custom.loading = false;
            syncControls();
            return;
        }
        showMessage(elements.customMessage, `正在读取槽位 ${slot}…`);
        try {
            const response = await fetch(`/api/personalization/photo/${slot}`, { cache: "no-store" });
            if (!response.ok) throw Object.assign(new Error(await responseError(response)),
                { code: response.status });
            const bytes = new Uint8Array(await response.arrayBuffer());
            const source = document.createElement("canvas");
            source.width = CUSTOM_WIDTH;
            source.height = CUSTOM_HEIGHT;
            source.getContext("2d").putImageData(
                root.BadgeImage.rgb565ToImageData(bytes, CUSTOM_WIDTH, CUSTOM_HEIGHT), 0, 0);
            if (generation !== custom.loadGeneration || slot !== custom.slot) return;
            custom.image = source;
            custom.hasImage = true;
            custom.dirty = false;
            redrawCustom();
            showMessage(elements.customMessage, `槽位 ${slot} 已载入，可继续调整或覆盖`);
        } catch (error) {
            if (generation !== custom.loadGeneration) return;
            if (error.code === 404) custom.slots = updateSlotOccupation(custom.slots, slot, false);
            showMessage(elements.customMessage, error.message || "读取图片失败，请重试", "error");
        } finally {
            if (generation === custom.loadGeneration) {
                custom.loading = false;
                syncControls();
            }
        }
    }

    async function selectCustomSlot(slot) {
        if (!validSlot(slot) || slot === custom.slot || mutationActive() || custom.loading) return;
        if (custom.dirty && !root.confirm(`槽位 ${custom.slot} 的修改尚未保存，确定放弃吗？`)) return;
        custom.slot = slot;
        await loadCustomSlot(slot);
    }

    elements.slots.forEach((button) => button.addEventListener("click", () => {
        selectCustomSlot(Number(button.dataset.slot));
    }));
    elements.customFile.addEventListener("change", async () => {
        const file = elements.customFile.files[0];
        if (!file) return;
        /* Decode is asynchronous; retain the initiating slot instead of consulting later UI state. */
        const targetSlot = custom.slot;
        custom.loading = true;
        syncControls();
        try {
            const decoded = await root.BadgeImage.decodeFile(file);
            if (targetSlot !== custom.slot) {
                closeImage(decoded);
                return;
            }
            closeImage(custom.image);
            custom.image = decoded;
            resetTransform(custom, elements.customZoom);
            custom.hasImage = true;
            custom.dirty = true;
            redrawCustom();
            showMessage(elements.customMessage,
                `图片已载入，调整后可保存到槽位 ${targetSlot}`);
        } catch (error) {
            showMessage(elements.customMessage, error.message || "图片无法处理", "error");
        } finally {
            custom.loading = false;
            elements.customFile.value = "";
            syncControls();
        }
    });
    elements.customZoom.addEventListener("input", () => {
        custom.transform.zoom = Number(elements.customZoom.value);
        custom.dirty = true;
        redrawCustom();
    });
    elements.customRotate.addEventListener("click", () => {
        if (!custom.image) return;
        custom.transform = root.BadgeImage.rotateTransform(custom.transform, 1);
        custom.dirty = true;
        redrawCustom();
    });

    async function loadProfile() {
        const profileResponse = await fetch("/api/profile", { cache: "no-store" });
        if (!profileResponse.ok) throw new Error(await responseError(profileResponse, false, true));
        const payload = await profileResponse.json();
        const storedShape = normalizeProfileShape(payload.shape);
        elements.name.value = payload.name || "";
        elements.bio.value = payload.bio || "";
        profile.shape = storedShape;
        elements.shapes.forEach((input) => { input.checked = input.value === storedShape; });
        const photoResponse = await fetch("/api/photo", { cache: "no-store" });
        if (!photoResponse.ok) throw new Error(await responseError(photoResponse, false, true));
        const bytes = new Uint8Array(await photoResponse.arrayBuffer());
        const source = document.createElement("canvas");
        source.width = source.height = 200;
        source.getContext("2d").putImageData(
            root.BadgeImage.rgb565ToImageData(bytes, 200, 200), 0, 0);
        closeImage(profile.image);
        profile.image = source;
        profile.sourceShape = storedShape;
        profile.hasImage = true;
        resetTransform(profile, elements.profileZoom);
        /* Stored RGB565 has no alpha, so reapply its saved mask before showing the reflected image. */
        redrawProfile();
        profile.dirty = false;
        profile.needsReload = false;
        syncControls();
    }

    async function loadCustomMetadata() {
        const response = await fetch("/api/personalization", { cache: "no-store" });
        if (!response.ok) throw new Error(await responseError(response));
        custom.slots = normalizePersonalizationMetadata(await response.json());
        custom.needsReload = false;
        syncControls();
    }

    async function switchTab(target) {
        if (target === session.activeTab || mutationActive()) return;
        const current = session.activeTab === "profile" ? profile : custom;
        if (current.dirty && !root.confirm("当前修改尚未保存，确定放弃吗？")) return;
        if (current.dirty) {
            current.dirty = false;
            current.needsReload = true;
        }
        session.activeTab = target;
        const profileActive = target === "profile";
        elements.profileForm.hidden = !profileActive;
        elements.customForm.hidden = profileActive;
        elements.profileTab.classList.toggle("active", profileActive);
        elements.customTab.classList.toggle("active", !profileActive);
        elements.profileTab.setAttribute("aria-selected", String(profileActive));
        elements.customTab.setAttribute("aria-selected", String(!profileActive));
        if (profileActive && profile.needsReload) {
            try { await loadProfile(); } catch (_) {
                showMessage(elements.profileMessage, "无法重新读取工牌资料", "error");
            }
        } else if (!profileActive && (custom.needsReload || custom.loadedSlot !== custom.slot)) {
            await loadCustomSlot(custom.slot);
            custom.needsReload = false;
        }
    }

    elements.profileTab.addEventListener("click", () => { switchTab("profile"); });
    elements.customTab.addEventListener("click", () => { switchTab("custom"); });

    elements.profileForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        if (saveDisabled(Object.assign({}, profile, { uploading: mutationActive() }))) return;
        profile.uploading = true;
        syncControls();
        showMessage(elements.profileMessage, "正在保存，请勿关闭页面…");
        const rgba = profileContext.getImageData(0, 0, 200, 200).data;
        const body = buildEnvelope(profile.name, profile.bio,
            root.BadgeImage.rgbaToRgb565(rgba, 200 * 200), profile.shape);
        try {
            const response = await fetch("/api/profile", { method: "POST",
                headers: { "Content-Type": "application/octet-stream",
                    "X-Passport-Session": session.token }, body });
            if (!response.ok) throw Object.assign(
                new Error(await responseError(response, false, true)), { known: true });
            profile.dirty = false;
            showMessage(elements.profileMessage, "已保存，可继续修改或按设备任意键退出", "success");
        } catch (error) {
            showMessage(elements.profileMessage,
                error.known ? error.message : "连接已中断，保存结果未知，请查看设备工牌", "error");
        } finally {
            profile.uploading = false;
            syncControls();
        }
    });

    elements.customForm.addEventListener("submit", async (event) => {
        event.preventDefault();
        const targetSlot = custom.slot;
        if (personalizationSaveDisabled({ hasImage: custom.hasImage, slot: targetSlot,
            token: session.token, mutating: mutationActive(), loading: custom.loading })) return;
        custom.mutating = true;
        syncControls();
        showMessage(elements.customMessage, `正在保存到槽位 ${targetSlot}，请勿关闭页面…`);
        const rgba = customContext.getImageData(0, 0, CUSTOM_WIDTH, CUSTOM_HEIGHT).data;
        const pixels = root.BadgeImage.rgbaToRgb565(rgba, CUSTOM_WIDTH * CUSTOM_HEIGHT);
        const request = personalizationRequest("POST", targetSlot, session.token, pixels);
        try {
            const response = await fetch(request.path, request.options);
            if (!response.ok) throw Object.assign(
                new Error(await responseError(response)), { known: true });
            custom.slots = updateSlotOccupation(custom.slots, targetSlot, true);
            custom.dirty = false;
            showMessage(elements.customMessage,
                `槽位 ${targetSlot} 已保存，可继续调整或按设备任意键退出`, "success");
        } catch (error) {
            showMessage(elements.customMessage, error.known ? error.message :
                "连接已中断，保存结果未知，请重新读取槽位确认", "error");
        } finally {
            custom.mutating = false;
            syncControls();
        }
    });

    elements.customClear.addEventListener("click", async () => {
        const targetSlot = custom.slot;
        if (!session.token || mutationActive() || custom.loading ||
            !confirmSlotClear(targetSlot, custom.slots[targetSlot - 1],
                (text) => root.confirm(text))) return;
        custom.mutating = true;
        syncControls();
        showMessage(elements.customMessage, `正在清空槽位 ${targetSlot}…`);
        const request = personalizationRequest("DELETE", targetSlot, session.token);
        try {
            const response = await fetch(request.path, request.options);
            if (!response.ok) throw Object.assign(
                new Error(await responseError(response, true)), { known: true });
            custom.slots = updateSlotOccupation(custom.slots, targetSlot, false);
            clearCustomEditor();
            showMessage(elements.customMessage, `槽位 ${targetSlot} 已清空`, "success");
        } catch (error) {
            showMessage(elements.customMessage, error.known ? error.message :
                "连接已中断，清空结果未知；原图片可能仍保留，请重新读取确认", "error");
        } finally {
            custom.mutating = false;
            syncControls();
        }
    });

    async function loadInitial() {
        try {
            const statusResponse = await fetch("/api/status", { cache: "no-store" });
            if (!statusResponse.ok) throw new Error("无法读取设备状态");
            const status = await statusResponse.json();
            /* The token exists only for this physical hotspot session and is never persisted locally. */
            session.token = String(status.token || "");
            elements.connection.textContent = status.connected ? "手机已连接" : "已连接设备热点";
            await loadProfile();
            try { await loadCustomMetadata(); } catch (error) {
                showMessage(elements.customMessage, error.message || "无法读取个性化槽位", "error");
            }
        } catch (error) {
            elements.connection.textContent = "设备连接异常";
            showMessage(elements.profileMessage, error.message || "页面初始化失败", "error");
        } finally {
            syncControls();
        }
    }

    syncControls();
    loadInitial();
})(typeof globalThis !== "undefined" ? globalThis : window);
