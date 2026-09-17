// Minimal BlurHash decoder (compatible with the BlurHash spec).
// Decodes a BlurHash string into raw RGBA pixels for a placeholder canvas.
// Throws on malformed input; callers should fall back to plain <img> rendering.

const DIGIT_CHARACTERS =
  '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz#$%*+,-.:;=?@[]^_{|}~';

function decode83(str, start, end) {
  let value = 0;
  for (let i = start; i < end; i++) {
    const digit = DIGIT_CHARACTERS.indexOf(str[i]);
    if (digit < 0) throw new Error('Invalid BlurHash character');
    value = value * 83 + digit;
  }
  return value;
}

function sRGBToLinear(value) {
  const v = value / 255;
  return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4);
}

function linearTosRGB(value) {
  const v = value < 0 ? 0 : value > 1 ? 1 : value;
  return (v <= 0.0031308 ? v * 12.92 : 1.055 * Math.pow(v, 1 / 2.4) - 0.055) * 255;
}

function signPow(value, exp) {
  return (value < 0 ? -1 : 1) * Math.pow(value < 0 ? -value : value, exp);
}

export function decodeBlurHash(hash, width, height, punch = 1) {
  if (!hash || hash.length < 6) throw new Error('BlurHash too short');

  const sizeFlag = decode83(hash, 0, 1);
  const numY = Math.floor(sizeFlag / 9) + 1;
  const numX = (sizeFlag % 9) + 1;

  if (hash.length !== 4 + 2 * numX * numY) throw new Error('BlurHash length mismatch');

  const quantisedMaximumValue = decode83(hash, 1, 2);
  const maximumValue = (quantisedMaximumValue + 1) / 166;

  const colors = new Array(numX * numY);
  for (let i = 0; i < numX * numY; i++) {
    if (i === 0) {
      const value = decode83(hash, 2, 6);
      colors[i] = [
        sRGBToLinear(value >> 16),
        sRGBToLinear((value >> 8) & 255),
        sRGBToLinear(value & 255)
      ];
    } else {
      const value = decode83(hash, 4 + i * 2, 6 + i * 2);
      colors[i] = [
        signPow((Math.floor(value / (19 * 19)) - 9) / 9, 0.5) * maximumValue * punch,
        signPow((Math.floor(value / 19) % 19 - 9) / 9, 0.5) * maximumValue * punch,
        signPow(((value % 19) - 9) / 9, 0.5) * maximumValue * punch
      ];
    }
  }

  // Precompute cos tables for inner DCT loops
  const cosX = new Float32Array(numX * width);
  for (let i = 0; i < numX; i++) {
    for (let x = 0; x < width; x++) {
      cosX[i * width + x] = Math.cos((Math.PI * i * x) / width);
    }
  }
  const cosY = new Float32Array(numY * height);
  for (let j = 0; j < numY; j++) {
    for (let y = 0; y < height; y++) {
      cosY[j * height + y] = Math.cos((Math.PI * j * y) / height);
    }
  }

  const bytesPerRow = width * 4;
  const pixels = new Uint8ClampedArray(bytesPerRow * height);
  let pIdx = 0;
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let r = 0;
      let g = 0;
      let b = 0;
      for (let j = 0; j < numY; j++) {
        const basisY = cosY[j * height + y];
        for (let i = 0; i < numX; i++) {
          const basis = cosX[i * width + x] * basisY;
          const color = colors[j * numX + i];
          r += color[0] * basis;
          g += color[1] * basis;
          b += color[2] * basis;
        }
      }
      pixels[pIdx++] = linearTosRGB(r);
      pixels[pIdx++] = linearTosRGB(g);
      pixels[pIdx++] = linearTosRGB(b);
      pixels[pIdx++] = 255;
    }
  }
  return pixels;
}
