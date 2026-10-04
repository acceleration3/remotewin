#pragma once

class QImage;
class QPainter;

namespace rwin::host {

class PixelMapping;

// Draws a guest frame where ``where`` puts it.
//
// At a fit of 1 every guest pixel lands on exactly one device pixel: the
// frame is drawn into a logical rectangle whose device size is its own
// size, from a whole device pixel, with no smoothing - so the painter's
// scale and the rectangle's cancel and nothing is resampled. Drawn at its
// logical size instead, as it once was, a frame on a screen scaled to 120%
// was scaled by 1.2 nearest-neighbour: every fifth row and column doubled,
// some pixels stretched and others not. Fitted, the picture is resampled
// smoothly, which is the best a scale that is not whole can do.
//
// The frame keeps its own device pixel ratio of 1. Setting the screen's on
// it would say the same thing, but a frame is shared with the signal that
// delivered it and changing it would copy every frame.
void draw_picture(QPainter& painter, const QImage& frame, const PixelMapping& where);

}  // namespace rwin::host
