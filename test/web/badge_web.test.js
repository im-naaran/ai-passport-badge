"use strict";

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

require("../../main/web/badge/badge_image.js");
require("../../main/web/badge/app.js");

const image = globalThis.BadgeImage;
const app = globalThis.BadgeAppLogic;

test("bio validation uses UTF-8 bytes and permits empty text", () => {
    assert.equal(app.validBio(""), true);
    assert.equal(app.bioBytes("保持好奇").length, 12);
    assert.equal(app.validBio("a".repeat(96)), true);
    assert.equal(app.validBio("a".repeat(97)), false);
    assert.equal(app.validBio("a\nb"), false);
});

test("version 3 envelope stores name bio shape and fixed image length", () => {
    const pixels = new Uint8Array(80000);
    for (const [shape, value] of [["square", 0], ["rounded", 1], ["circle", 2]]) {
        const body = app.buildEnvelope(" 张三 ", " 保持好奇 ", pixels, shape);
        const view = new DataView(body.buffer);
        assert.equal(view.getUint32(0, true), 0x46525042);
        assert.equal(view.getUint16(4, true), 3);
        assert.equal(view.getUint16(6, true), 6);
        assert.equal(view.getUint16(8, true), 12);
        assert.equal(view.getUint8(10), value);
        assert.equal(view.getUint8(11), 0);
        assert.equal(view.getUint32(12, true), 80000);
        assert.equal(body.length, 16 + 6 + 12 + 80000);
    }
    assert.throws(() => app.buildEnvelope("张三", "", pixels, "unknown"), /不兼容/);
});

test("save state includes optional bio validation", () => {
    const valid = { hasImage: true, name: "张三", bio: "", uploading: false,
        shape: "square", sourceShape: null };
    assert.equal(app.saveDisabled(valid), false);
    assert.equal(app.saveDisabled({ ...valid, bio: "a".repeat(97) }), true);
    assert.equal(app.saveDisabled({ ...valid, shape: "unknown" }), true);
});

test("profile shape parsing is strict and supports radio initialization values", () => {
    for (const shape of ["square", "rounded", "circle"])
        assert.equal(app.normalizeProfileShape(shape), shape);
    assert.throws(() => app.normalizeProfileShape(undefined), /不兼容/);
    assert.throws(() => app.normalizeProfileShape("unknown"), /不兼容/);
});

test("device-reflected masks cannot expand until a local original is selected", () => {
    assert.equal(app.shapeExpansionBlocked("circle", "rounded"), true);
    assert.equal(app.shapeExpansionBlocked("circle", "square"), true);
    assert.equal(app.shapeExpansionBlocked("rounded", "square"), true);
    assert.equal(app.shapeExpansionBlocked("square", "rounded"), false);
    assert.equal(app.shapeExpansionBlocked("square", "circle"), false);
    assert.equal(app.shapeExpansionBlocked("rounded", "circle"), false);
    assert.equal(app.shapeExpansionBlocked("circle", "circle"), false);
    assert.equal(app.shapeExpansionBlocked(null, "square"), false);
    assert.equal(app.saveDisabled({ hasImage: true, name: "张三", bio: "", uploading: false,
        shape: "square", sourceShape: "circle" }), true);
});

test("personalization metadata requires the fixed three-slot screen contract", () => {
    const metadata = { slotCount: 3, width: 240, height: 320, imageBytes: 153600,
        slots: [{ slot: 1, occupied: true }, { slot: 2, occupied: false },
            { slot: 3, occupied: true }] };
    assert.deepEqual(app.normalizePersonalizationMetadata(metadata), [true, false, true]);
    assert.throws(() => app.normalizePersonalizationMetadata(
        { ...metadata, width: 320 }), /不兼容/);
    assert.throws(() => app.normalizePersonalizationMetadata(
        { ...metadata, slots: metadata.slots.slice(0, 2) }), /状态无效/);
});

test("personalization requests capture slot token method and exact body", () => {
    const pixels = new Uint8Array(153600);
    const save = app.personalizationRequest("POST", 2, "1234", pixels);
    assert.equal(save.path, "/api/personalization/slot/2");
    assert.equal(save.options.method, "POST");
    assert.equal(save.options.headers["X-Passport-Session"], "1234");
    assert.equal(save.options.headers["Content-Type"], "application/octet-stream");
    assert.equal(save.options.body, pixels);
    const clear = app.personalizationRequest("DELETE", 3, "1234");
    assert.equal(clear.path, "/api/personalization/slot/3");
    assert.equal(clear.options.method, "DELETE");
    assert.equal("body" in clear.options, false);
    assert.throws(() => app.personalizationRequest("POST", 1, "1234", new Uint8Array(2)),
        /invalid personalization image/);
});

test("personalization state updates only the target slot and locks mutation targets", () => {
    const before = [true, false, true];
    const after = app.updateSlotOccupation(before, 2, true);
    assert.deepEqual(before, [true, false, true]);
    assert.deepEqual(after, [true, true, true]);
    assert.equal(app.personalizationSaveDisabled(
        { hasImage: true, slot: 2, token: "1", mutating: false, loading: false }), false);
    assert.equal(app.personalizationSaveDisabled(
        { hasImage: true, slot: 2, token: "1", mutating: true, loading: false }), true);
    assert.equal(app.personalizationSaveDisabled(
        { hasImage: true, slot: 2, token: "", mutating: false, loading: false }), true);
});

test("slot clearing requires occupied state and a slot-specific confirmation", () => {
    let prompt = "";
    assert.equal(app.confirmSlotClear(2, true, (text) => { prompt = text; return false; }), false);
    assert.match(prompt, /槽位 2/);
    assert.equal(app.confirmSlotClear(2, true, () => true), true);
    assert.equal(app.confirmSlotClear(2, false, () => true), false);
    assert.match(app.personalizationErrorMessage("storage_error", 500, true), /原图片仍保留/);
});

test("shape geometry covers square rounded and circle boundaries", () => {
    assert.equal(image.normalizedShape("unknown"), "square");
    assert.equal(image.shapeContains("square", 0, 0), true);
    assert.equal(image.shapeContains("rounded", 0, 0), false);
    assert.equal(image.shapeContains("rounded", 24, 0), true);
    assert.equal(image.shapeContains("circle", 0, 0), false);
    assert.equal(image.shapeContains("circle", 100, 100), true);
    assert.equal(image.ROUNDED_RADIUS, 24);
});

test("RGB565 conversion remains fixed at 80000 bytes", () => {
    const rgba = new Uint8ClampedArray(200 * 200 * 4);
    rgba.fill(255);
    assert.equal(image.rgbaToRgb565(rgba, 200 * 200).length, 80000);
});

test("rectangular cover geometry handles rotation zoom and pan bounds", () => {
    const portrait = image.coverGeometry(600, 800,
        { rotation: 0, zoom: 1, panX: 999, panY: -999 }, 240, 320);
    assert.equal(portrait.scale, 0.4);
    assert.equal(portrait.renderedWidth, 240);
    assert.equal(portrait.renderedHeight, 320);
    assert.equal(portrait.panX, 0);
    assert.equal(portrait.panY, 0);

    const rotated = image.coverGeometry(1200, 400,
        { rotation: 90, zoom: 2, panX: 999, panY: -999 }, 240, 320);
    assert.equal(rotated.rotation, 90);
    assert.equal(rotated.renderedWidth, 480);
    assert.equal(rotated.renderedHeight, 1440);
    assert.equal(rotated.panX, 120);
    assert.equal(rotated.panY, -560);
});

test("extreme aspect ratios remain covered inside a 240 by 320 canvas", () => {
    for (const [width, height] of [[4000, 100], [100, 4000]]) {
        const geometry = image.coverGeometry(width, height,
            { rotation: 0, zoom: 1, panX: 1e6, panY: 1e6 }, 240, 320);
        assert.ok(geometry.renderedWidth >= 240);
        assert.ok(geometry.renderedHeight >= 320);
        assert.ok(Math.abs(geometry.panX) <= geometry.maxPanX);
        assert.ok(Math.abs(geometry.panY) <= geometry.maxPanY);
    }
});

test("pointer deltas scale from CSS pixels to backing canvas pixels", () => {
    assert.deepEqual(image.canvasPointerDelta(12, -16, 240, 320, 120, 160),
        { x: 24, y: -32 });
    assert.throws(() => image.canvasPointerDelta(1, 1, 240, 320, 0, 160), RangeError);
});

test("240 by 320 RGB565 output has fixed length and round-trips quantized pixels", () => {
    const rgba = new Uint8ClampedArray(240 * 320 * 4);
    for (let index = 0; index < rgba.length; index += 4) {
        rgba[index] = 255;
        rgba[index + 1] = 128;
        rgba[index + 2] = 0;
        rgba[index + 3] = 255;
    }
    const bytes = image.rgbaToRgb565(rgba, 240 * 320);
    assert.equal(bytes.length, 153600);
    const decoded = image.rgb565ToImageData(bytes, 240, 320, null);
    assert.equal(decoded.data.length, 240 * 320 * 4);
    assert.deepEqual(Array.from(decoded.data.slice(0, 4)), [255, 130, 0, 255]);
});

test("transparent mask pixels match the device canvas instead of white", () => {
    const transparent = new Uint8ClampedArray([255, 255, 255, 0]);
    const rgb565 = image.rgbaToRgb565(transparent, 1);
    assert.deepEqual(image.DEVICE_CANVAS_RGB, [0x0b, 0x0f, 0x0e]);
    assert.deepEqual(Array.from(rgb565), [0x61, 0x08]);
});

test("profile form asks for name before bio", () => {
    const html = fs.readFileSync(path.join(__dirname, "../../main/web/badge/index.html"), "utf8");
    assert.ok(html.indexOf('id="name"') < html.indexOf('id="bio"'));
});

test("page defaults to profile and exposes three personalization slots", () => {
    const html = fs.readFileSync(path.join(__dirname, "../../main/web/badge/index.html"), "utf8");
    assert.match(html, /id="profile-tab"[^>]+aria-selected="true"/);
    assert.match(html, /id="personalization-form" hidden/);
    assert.equal((html.match(/data-slot="[123]"/g) || []).length, 3);
    assert.match(html, /id="personalization-preview"[^>]+width="240" height="320"/);
    assert.match(html, /id="personalization-clear"/);
});

test("web assets remain offline and personalization canvas scales on narrow screens", () => {
    const files = ["index.html", "style.css", "app.js", "badge_image.js"].map((name) =>
        fs.readFileSync(path.join(__dirname, `../../main/web/badge/${name}`), "utf8"));
    for (const source of files) assert.doesNotMatch(source, /(?:src|href|fetch)\s*=\s*["']https?:/i);
    assert.match(files[1], /\.custom-canvas\s*\{[^}]*width:\s*min\(240px,\s*100%\)/s);
});

test("embedded page cache-busts all static resources", () => {
    const html = fs.readFileSync(path.join(__dirname, "../../main/web/badge/index.html"), "utf8");
    assert.match(html, /Cache-Control[^>]+no-store/);
    assert.match(html, /href="\/style\.css\?v=\d+"/);
    assert.match(html, /src="\/badge_image\.js\?v=\d+"/);
    assert.match(html, /src="\/app\.js\?v=\d+"/);
});

test("transparent crop area is not painted by the preview stylesheet", () => {
    const css = fs.readFileSync(path.join(__dirname, "../../main/web/badge/style.css"), "utf8");
    const canvasRule = css.match(/canvas\s*\{([^}]*)\}/);
    assert.ok(canvasRule);
    assert.match(canvasRule[1], /background:\s*transparent/);
    assert.doesNotMatch(canvasRule[1], /box-shadow/);
});
