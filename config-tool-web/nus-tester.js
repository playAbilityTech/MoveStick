import crc32 from "./crc.js";

const NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const NUS_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

const NUS_PROTOCOL_VERSION = 1;
const NUS_REPORT_ID = 1;

const SLIP_END = 0xc0;
const SLIP_ESC = 0xdb;
const SLIP_ESC_END = 0xdc;
const SLIP_ESC_ESC = 0xdd;
const NUS_TESTER_ENABLED_STORAGE_KEY = "hid-remapper-nus-tester-enabled";

setupFloatingTester();

const statusEl = document.getElementById("nus_status");
const connectButton = document.getElementById("nus_connect");
const connectFilteredButton = document.getElementById("nus_connect_filtered");
const disconnectButton = document.getElementById("nus_disconnect");
const pairButton = document.getElementById("nus_pair");
const descriptorSelect = document.getElementById("nus_descriptor");

let device;
let server;
let rxCharacteristic;
let report = neutralReport();
let encryptedLinkReady = false;
let reportDirty = false;
let writeInFlight = false;

function setupFloatingTester() {
    const bluetoothTab = document.getElementById("nav-bluetooth");
    if (!bluetoothTab) {
        return;
    }

    const heading = Array.from(bluetoothTab.querySelectorAll("h5"))
        .find((element) => element.textContent.trim() === "NUS tester");
    if (!heading || document.getElementById("nus_tester_floating")) {
        return;
    }

    const floating = document.createElement("div");
    floating.id = "nus_tester_floating";
    floating.className = "nus-floating";

    const panel = document.createElement("div");
    panel.id = "nus_tester_panel";
    panel.className = "nus-floating-panel bg-white border rounded shadow";

    const panelHeader = document.createElement("div");
    panelHeader.className = "d-flex align-items-center justify-content-between border-bottom px-3 py-2";

    const panelTitle = document.createElement("h5");
    panelTitle.className = "m-0";
    panelTitle.textContent = "NUS tester";

    const closeButton = document.createElement("button");
    closeButton.id = "nus_tester_close";
    closeButton.type = "button";
    closeButton.className = "btn-close";
    closeButton.setAttribute("aria-label", "Hide NUS tester");

    const panelBody = document.createElement("div");
    panelBody.className = "p-3";

    panelHeader.append(panelTitle, closeButton);
    panel.append(panelHeader, panelBody);
    floating.append(panel);

    let node = heading.nextSibling;
    heading.remove();
    while (node) {
        const nextNode = node.nextSibling;
        panelBody.appendChild(node);
        node = nextNode;
    }

    const intro = panelBody.querySelector("p.text-muted");
    if (intro) {
        intro.classList.add("small", "mb-3");
    }

    for (const button of panelBody.querySelectorAll(".btn")) {
        button.classList.add("btn-sm");
    }

    const status = panelBody.querySelector("#nus_status");
    if (status) {
        status.classList.add("small", "mb-0");
    }

    document.body.appendChild(floating);

    const enabledCheckbox = document.getElementById("nus_tester_enabled");
    const setEnabled = (enabled) => {
        floating.classList.toggle("d-none", !enabled);
        if (enabledCheckbox) {
            enabledCheckbox.checked = enabled;
        }
        try {
            localStorage.setItem(NUS_TESTER_ENABLED_STORAGE_KEY, enabled ? "1" : "0");
        } catch {
        }
    };

    let startEnabled = false;
    try {
        startEnabled = localStorage.getItem(NUS_TESTER_ENABLED_STORAGE_KEY) === "1";
    } catch {
    }

    enabledCheckbox?.addEventListener("change", () => setEnabled(enabledCheckbox.checked));
    closeButton.addEventListener("click", () => setEnabled(false));
    setEnabled(startEnabled);
}

function setStatus(message) {
    statusEl.textContent = message;
}

function neutralReport() {
    return new Uint8Array([0x00, 0x00, 0x0f, 0x80, 0x80, 0x80, 0x80, 0x00]);
}

function setConnectedState(connected) {
    connectButton.disabled = connected;
    connectFilteredButton.disabled = connected;
    disconnectButton.disabled = !connected;
    pairButton.disabled = !connected;
}

function formatWriteError(error) {
    const message = error.message || String(error);

    if (error.name === "NetworkError" || /encrypt|security|auth|pair/i.test(message)) {
        return `${message}. The firmware requires an encrypted BLE link; accept the browser or OS pairing prompt, then try again.`;
    }

    if (error.name === "NotAllowedError") {
        return `${message}. Bluetooth access or pairing was cancelled.`;
    }

    return message;
}

function framePacket(payload) {
    const packet = new Uint8Array(payload.length + 4);
    packet.set(payload);

    const view = new DataView(packet.buffer);
    const crc = crc32(new DataView(payload.buffer, payload.byteOffset, payload.byteLength), payload.length);
    view.setUint32(payload.length, crc, true);

    const framed = [SLIP_END];
    for (const byte of packet) {
        if (byte === SLIP_END) {
            framed.push(SLIP_ESC, SLIP_ESC_END);
        } else if (byte === SLIP_ESC) {
            framed.push(SLIP_ESC, SLIP_ESC_ESC);
        } else {
            framed.push(byte);
        }
    }
    framed.push(SLIP_END);
    return new Uint8Array(framed);
}

async function sendCurrentReport(requireResponse = false) {
    if (!rxCharacteristic) {
        setStatus("Not connected");
        return;
    }

    const payload = new Uint8Array(4 + report.length);
    payload[0] = NUS_PROTOCOL_VERSION;
    payload[1] = Number(descriptorSelect.value);
    payload[2] = report.length;
    payload[3] = NUS_REPORT_ID;
    payload.set(report, 4);

    const framed = framePacket(payload);
    const useResponse = requireResponse || !encryptedLinkReady;
    const t0 = performance.now();

    if (useResponse && rxCharacteristic.writeValueWithResponse) {
        await rxCharacteristic.writeValueWithResponse(framed);
        encryptedLinkReady = true;
    } else if (rxCharacteristic.writeValueWithoutResponse) {
        await rxCharacteristic.writeValueWithoutResponse(framed);
    } else if (rxCharacteristic.writeValue) {
        await rxCharacteristic.writeValue(framed);
    } else if (rxCharacteristic.writeValueWithResponse) {
        await rxCharacteristic.writeValueWithResponse(framed);
        encryptedLinkReady = true;
    }

    const elapsedMs = performance.now() - t0;
    const mode = useResponse ? "encrypted write" : "fast write";
    setStatus(`Sent ${report.length} byte report (${mode}, ${elapsedMs.toFixed(1)} ms) to ${device.name || "HID Remapper"}`);
}

async function flushReportWrites(requireResponse = false) {
    writeInFlight = true;
    try {
        while (reportDirty) {
            reportDirty = false;
            const useResponse = requireResponse || !encryptedLinkReady;
            await sendCurrentReport(useResponse);
            requireResponse = false;
        }
    } catch (error) {
        if (requireResponse || !encryptedLinkReady) {
            encryptedLinkReady = false;
        }
        setStatus(formatWriteError(error));
    } finally {
        writeInFlight = false;
        if (reportDirty) {
            await flushReportWrites(false);
        }
    }
}

function queueCurrentReport(requireResponse = false) {
    reportDirty = true;
    if (writeInFlight) {
        return Promise.resolve();
    }
    return flushReportWrites(requireResponse);
}

function notifyNusConnectionChanged(connected) {
    document.dispatchEvent(new CustomEvent("nus-connection-changed", { detail: { connected } }));
}

function onDisconnected() {
    rxCharacteristic = undefined;
    server = undefined;
    encryptedLinkReady = false;
    reportDirty = false;
    writeInFlight = false;
    setConnectedState(false);
    setStatus("Disconnected");
    notifyNusConnectionChanged(false);
}

async function connectWithOptions(options) {
    try {
        if (!navigator.bluetooth) {
            setStatus("Web Bluetooth is not available in this browser");
            return;
        }

        setStatus("Opening Bluetooth picker...");
        device = await navigator.bluetooth.requestDevice(options);
        setStatus(`Selected ${device.name || device.id}; connecting GATT...`);
        device.addEventListener("gattserverdisconnected", onDisconnected);

        server = await device.gatt.connect();
        setStatus(`Connected to ${device.name || device.id}; opening NUS service...`);
        const service = await server.getPrimaryService(NUS_SERVICE_UUID);
        setStatus(`NUS service found on ${device.name || device.id}; opening write characteristic...`);
        rxCharacteristic = await service.getCharacteristic(NUS_RX_UUID);

        encryptedLinkReady = false;
        setConnectedState(true);
        notifyNusConnectionChanged(true);
        setStatus(`Connected to ${device.name || "HID Remapper"}. Use Pair / Send neutral to trigger encryption, then reports use fast writes.`);
    } catch (error) {
        setStatus(error.message || String(error));
    }
}

connectButton.addEventListener("click", async () => {
    await connectWithOptions({
        acceptAllDevices: true,
        optionalServices: [NUS_SERVICE_UUID],
    });
});

connectFilteredButton.addEventListener("click", async () => {
    await connectWithOptions({
        filters: [{ services: [NUS_SERVICE_UUID] }],
        optionalServices: [NUS_SERVICE_UUID],
    });
});

disconnectButton.addEventListener("click", () => {
    if (device?.gatt?.connected) {
        device.gatt.disconnect();
    }
});

pairButton.addEventListener("click", async () => {
    report = neutralReport();
    encryptedLinkReady = false;
    await queueCurrentReport(true);
});

document.getElementById("nus_neutral").addEventListener("click", async () => {
    report = neutralReport();
    for (const slider of document.querySelectorAll("input.nus_axis")) {
        slider.value = 128;
    }
    await queueCurrentReport();
});

for (const button of document.querySelectorAll("[data-nus-button]")) {
    const bit = Number(button.dataset.nusButton);
    button.addEventListener("click", async () => {
        report[0] |= 1 << bit;
        await queueCurrentReport();
        report[0] &= ~(1 << bit);
        await queueCurrentReport();
    });
}

for (const button of document.querySelectorAll("[data-nus-hat]")) {
    button.addEventListener("click", async () => {
        report[2] = Number(button.dataset.nusHat);
        await queueCurrentReport();
    });
}

for (const slider of document.querySelectorAll("[data-nus-axis]")) {
    const index = Number(slider.dataset.nusAxis);
    slider.addEventListener("input", async (event) => {
        report[index] = Number(event.target.value);
        await queueCurrentReport();
    });
}
