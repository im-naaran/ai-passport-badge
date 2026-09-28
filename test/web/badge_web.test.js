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

test("version 2 envelope stores name bio and fixed image length", () => {
    const pixels = new Uint8Array(80000);
    const body = app.buildEnvelope(" 张三 ", " 保持好奇 ", pixels);
    const view = new DataView(body.buffer);
    assert.equal(view.getUint32(0, true), 0x46525042);
    assert.equal(view.getUint16(4, true), 2);
    assert.equal(view.getUint16(6, true), 6);
    assert.equal(view.getUint16(8, true), 12);
    assert.equal(view.getUint32(10, true), 80000);
    assert.equal(body.length, 14 + 6 + 12 + 80000);
});

test("save state includes optional bio validation", () => {
    assert.equal(app.saveDisabled({ hasImage: true, name: "张三", bio: "", uploading: false }), false);
    assert.equal(app.saveDisabled({ hasImage: true, name: "张三", bio: "a".repeat(97), uploading: false }), true);
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
