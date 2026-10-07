.pragma library

// Lines and frames of whole device pixels (qt/docs/features/hidpi.md). At 125 % or 150 % a line or a frame of 1 logical
// pixel is 1.25 or 1.5 device pixels, which the GPU draws 1 or 2 pixels thick depending on where it lands; a whole
// number of device pixels covers exactly that many wherever it lies.

// `logical` pixels as a whole number of device pixels, rounded down (as thin as designed, never thinner than one;
// 0 stays 0): 1 is one device pixel at 100 % to 175 %, two at 200 %; 3 is four at 150 %.
function whole(logical, dpr) {
    return Math.max(Math.min(1, logical), Math.floor(logical * dpr)) / dpr
}
