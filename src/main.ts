const WIDTH = 64;
const HEIGHT = 64;

// TODO: Display some kind of loader/progress for the initial fetches.
// TODO: Display unhandled errors to the user.

function assertNotNull<T>(value: T | null | undefined): T {
  if (value == null) throw new Error();
  return value;
}

function clamp(value: number, min: number, max: number) {
  return value < min ? min : value > max ? max : value;
}

function hmmToMins(hmm: number): number {
  return Math.floor(hmm / 100) * 60 + (hmm % 100);
}

function minsToHmm(mins: number): number {
  return Math.floor(mins / 60) * 100 + (mins % 60);
}

function offsetHmm(hmm: number, deltaMinutes: number): number {
  // TODO: Check definition of JS % to see whether this can be simplified.
  return minsToHmm((((hmmToMins(hmm) + deltaMinutes) % 1440) + 1440) % 1440);
}

const display = assertNotNull(
  document.getElementById("display"),
) as HTMLCanvasElement;

const [openImageButton, saveImageButton] = ["open-image", "save-image"].map(
  (id) => assertNotNull(document.getElementById(id)) as HTMLButtonElement,
);

const [gainRange, gainNumber, timeMorning, timeEvening, timeZone] = [
  "gain-range",
  "gain-number",
  "time-morning",
  "time-evening",
].map((id) => assertNotNull(document.getElementById(id)) as HTMLInputElement);

let postImageController: AbortController | undefined;

openImageButton.addEventListener("click", () => {
  const input = document.createElement("input");
  input.setAttribute("type", "file");
  input.addEventListener("change", async () => {
    const file = input.files?.[0];
    if (file) {
      // createImageBitmap can not read SVG images directly from the Blob,
      // so we load everything into a HTML Image object first.
      const url = URL.createObjectURL(file);
      const imageObject = new Image();
      const imageLoaded = new Promise<Event>((resolve, reject) => {
        imageObject.onload = resolve;
        imageObject.onerror = reject;
      });
      imageObject.src = url;
      await imageLoaded.finally(() => {
        URL.revokeObjectURL(url);
      });

      const g = assertNotNull(display.getContext("2d"));
      g.fillStyle = "#000";
      g.fillRect(0, 0, WIDTH, HEIGHT);
      g.drawImage(imageObject, 0, 0, WIDTH, HEIGHT);

      postImageController?.abort();
      postImageController = new AbortController();

      fetch("/api/image", {
        method: "POST",
        body: g.getImageData(0, 0, WIDTH, HEIGHT).data,
        signal: postImageController.signal,
      });
    }
  });
  input.click();
});

saveImageButton.addEventListener("click", () => {
  const link = document.createElement("a");
  link.setAttribute("href", display.toDataURL("image/png"));
  link.setAttribute("download", "image.png");
  link.click();
});

function coalesceFetch<Args extends unknown[]>(
  url: RequestInfo | URL,
  map: (...args: Args) => Omit<RequestInit, "signal">,
): (...args: Args) => Promise<Response | null> {
  let controller: AbortController | undefined;
  return (...args) => {
    controller?.abort();
    controller = new AbortController();
    return fetch(url, { signal: controller.signal, ...map(...args) }).catch(
      (err) => {
        if ("name" in err && err.name === "AbortError") {
          return null;
        }
        throw err;
      },
    );
  };
}

const postGain = coalesceFetch("/api/gain", (value: number) => ({
  method: "POST",
  body: JSON.stringify({ value: value / 100 }),
}));

gainRange.addEventListener("input", () => {
  const value = parseFloat(gainRange.value);
  gainNumber.value = value.toFixed(0);
  postGain(value);
});

gainNumber.addEventListener("change", () => {
  const value = parseFloat(gainNumber.value);
  gainRange.value = value.toFixed(0);
  postGain(value);
});

const postMorning = coalesceFetch("/api/time", (value: unknown) => ({
  method: "POST",
  body: JSON.stringify(value),
}));

timeMorning.addEventListener("change", () => {
  const offset = new Date().getTimezoneOffset();
  const morning = offsetHmm(parseInt(timeMorning.value, 10), offset);
  postMorning({ morning });
});

const postEvening = coalesceFetch("/api/time", (value: unknown) => ({
  method: "POST",
  body: JSON.stringify(value),
}));

timeEvening.addEventListener("change", () => {
  const offset = new Date().getTimezoneOffset();
  const evening = offsetHmm(parseInt(timeEvening.value, 10), offset);
  postEvening({ evening });
});

fetch("/api/image")
  .then((res) => res.arrayBuffer())
  .then((buffer) => {
    const image = new ImageData(new Uint8ClampedArray(buffer), WIDTH, HEIGHT);
    const g = assertNotNull(display.getContext("2d"));
    g.fillStyle = "#000";
    g.fillRect(0, 0, WIDTH, HEIGHT);
    g.putImageData(image, 0, 0);
  });

fetch("/api/gain")
  .then((res) => res.json())
  .then((data) => {
    const value = Number(data.value) * 100;
    gainRange.value = value.toFixed(0);
    gainNumber.value = value.toFixed(0);
  });

fetch("/api/time")
  .then((res) => res.json())
  .then((data) => {
    const offset = new Date().getTimezoneOffset();
    timeMorning.value = offsetHmm(Number(data.morning), -offset).toFixed(0);
    timeEvening.value = offsetHmm(Number(data.evening), -offset).toFixed(0);
  });
