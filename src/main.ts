const WIDTH = 64;
const HEIGHT = 64;

function assertNotNull<T>(value: T | null | undefined): T {
  if (value == null) throw new Error();
  return value;
}

function clamp(value: number, min: number, max: number) {
  return value < min ? min : value > max ? max : value;
}

function imageToRGB565le(src: ImageData, gamma: (x: number) => number) {
  const dst = new DataView(new ArrayBuffer(2 * WIDTH * HEIGHT));
  for (let y = 0; y < HEIGHT; y++) {
    for (let x = 0; x < WIDTH; x++) {
      const i = y * WIDTH + x;
      const r = clamp(gamma(src.data[4 * i + 0]), 0, 255) | 0;
      const g = clamp(gamma(src.data[4 * i + 1]), 0, 255) | 0;
      const b = clamp(gamma(src.data[4 * i + 2]), 0, 255) | 0;
      // Ignore alpha.
      dst.setUint16(
        2 * i,
        (clamp(r >> 3, 0, 0x1f) << 11) |
          (clamp(g >> 2, 0, 0x3f) << 5) |
          (clamp(b >> 3, 0, 0x1f) << 0),
        true,
      );
    }
  }
  return dst.buffer;
}

function imageFromRGB565le(src: DataView) {
  const dst = new Uint8ClampedArray(4 * WIDTH * HEIGHT);
  for (let y = 0; y < HEIGHT; y++) {
    for (let x = 0; x < WIDTH; x++) {
      const i = y * WIDTH + x;
      const rgb = src.getUint16(2 * i, true);

      dst[4 * i + 0] = ((rgb & 0xf800) >> 11) << 3;
      dst[4 * i + 1] = ((rgb & 0x07e0) >> 5) << 2;
      dst[4 * i + 2] = ((rgb & 0x001f) >> 0) << 3;
      dst[4 * i + 3] = 0xff;
    }
  }
  return dst;
}

const picker = assertNotNull(
  document.getElementById("file"),
) as HTMLInputElement;
const intensity = assertNotNull(
  document.getElementById("intensity"),
) as HTMLInputElement;
const preview = assertNotNull(
  document.getElementById("preview"),
) as HTMLCanvasElement;

assertNotNull(document.getElementById("reset")).addEventListener(
  "click",
  () => {
    const g = assertNotNull(preview.getContext("2d"));
    g.clearRect(0, 0, WIDTH, HEIGHT);
  },
);
assertNotNull(document.getElementById("submit")).addEventListener(
  "click",
  async (event) => {
    event.preventDefault();

    const encoded = await updatePreview();
    await fetch("/image.bin", {
      method: "PUT",
      body: encoded,
    });
  },
);

const GAMMA_LUT = [
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3,
  3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 9, 9,
  9, 10, 10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17,
  18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25, 25, 26, 27, 27, 28,
  29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36, 37, 38, 39, 39, 40, 41, 42, 43,
  44, 45, 46, 47, 48, 49, 50, 50, 51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62,
  63, 64, 66, 67, 68, 69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86,
  87, 89, 90, 92, 93, 95, 96, 98, 99, 101, 102, 104, 105, 107, 109, 110, 112,
  114, 115, 117, 119, 120, 122, 124, 126, 127, 129, 131, 133, 135, 137, 138,
  140, 142, 144, 146, 148, 150, 152, 154, 156, 158, 160, 162, 164, 167, 169,
  171, 173, 175, 177, 180, 182, 184, 186, 189, 191, 193, 196, 198, 200, 203,
  205, 208, 210, 213, 215, 218, 220, 223, 225, 228, 231, 233, 236, 239, 241,
  244, 247, 249, 252, 255,
];

async function updatePreview() {
  /** @type {CanvasRenderingContext2D} */
  const g = assertNotNull(preview.getContext("2d"));
  const files = picker.files;
  if (!files || files.length != 1) {
    g.clearRect(0, 0, WIDTH, HEIGHT);
    return;
  }

  // createImageBitmap can not read SVG images directly from the Blob,
  // so we load everything into a HTML Image object first.
  const url = URL.createObjectURL(files[0]);
  const imageObject = new Image();
  const imageLoaded = new Promise<Event>((resolve, reject) => {
    imageObject.onload = resolve;
    imageObject.onerror = reject;
  });
  imageObject.src = url;
  await imageLoaded.finally(() => {
    URL.revokeObjectURL(url);
  });

  const original = await createImageBitmap(imageObject, {
    resizeWidth: WIDTH,
    resizeHeight: HEIGHT,
  });

  g.drawImage(original, 0, 0, WIDTH, HEIGHT);
  const scale = parseFloat(intensity.value);
  const encoded = imageToRGB565le(
    g.getImageData(0, 0, WIDTH, HEIGHT, {
      // TODO: vscode reports this as an unknown field; probably the dom lib version.
      pixelFormat: "rgba-unorm8",
    } as any),
    (x) => GAMMA_LUT[clamp(x * scale, 0, 255) | 0],
  );

  return encoded;
}
picker.addEventListener("change", updatePreview);
intensity.addEventListener("input", updatePreview);
