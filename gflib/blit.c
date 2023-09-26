#include "global.h"
#include "blit.h"

void BlitBitmapRect4BitWithoutColorKey(const struct Bitmap *src, struct Bitmap *dst, u16 srcX, u16 srcY, u16 dstX, u16 dstY, u16 width, u16 height)
{
    BlitBitmapRect4Bit(src, dst, srcX, srcY, dstX, dstY, width, height, 0xFF);
}

void BlitBitmapRect4Bit(const struct Bitmap *src, struct Bitmap *dst, u16 srcX, u16 srcY, u16 dstX, u16 dstY, u16 width, u16 height, u8 colorKey)
{
    s32 xEnd;
    s32 yEnd;
    s32 multiplierSrcY;
    s32 multiplierDstY;
    s32 loopSrcY, loopDstY;
    s32 loopSrcX, loopDstX;
    const u8 *pixelsSrc;
    u8 *pixelsDst;
    s32 toOrr;
    s32 toShift;

    if (dst->width - dstX < width)
        xEnd = dst->width - dstX + srcX;
    else
        xEnd = srcX + width;

    if (dst->height - dstY < height)
        yEnd = dst->height - dstY + srcY;
    else
        yEnd = height + srcY;

    multiplierSrcY = (src->width + (src->width & 7)) >> 3;
    multiplierDstY = (dst->width + (dst->width & 7)) >> 3;

    if (colorKey == 0xFF)
    {
        for (loopSrcY = srcY, loopDstY = dstY; loopSrcY < yEnd; loopSrcY++, loopDstY++)
        {
            for (loopSrcX = srcX, loopDstX = dstX; loopSrcX < xEnd; loopSrcX++, loopDstX++)
            {
                pixelsSrc = src->pixels + ((loopSrcX >> 1) & 3) + ((loopSrcX >> 3) << 5) + (((loopSrcY >> 3) * multiplierSrcY) << 5) + ((u32)(loopSrcY << 29) >> 27);
                pixelsDst = dst->pixels + ((loopDstX >> 1) & 3) + ((loopDstX >> 3) << 5) + (((loopDstY >> 3) * multiplierDstY) << 5) + ((u32)(loopDstY << 29) >> 27);
                toOrr = (*pixelsSrc >> ((loopSrcX & 1) * 4)) & 0xF;
                toShift = (loopDstX & 1) * 4;
                *pixelsDst = (toOrr << toShift) | (*pixelsDst & (0xF0 >> toShift));
            }
        }
    }
    else
    {
        for (loopSrcY = srcY, loopDstY = dstY; loopSrcY < yEnd; loopSrcY++, loopDstY++)
        {
            for (loopSrcX = srcX, loopDstX = dstX; loopSrcX < xEnd; loopSrcX++, loopDstX++)
            {
                pixelsSrc = src->pixels + ((loopSrcX >> 1) & 3) + ((loopSrcX >> 3) << 5) + (((loopSrcY >> 3) * multiplierSrcY) << 5) + ((u32)(loopSrcY << 29) >> 27);
                pixelsDst = dst->pixels + ((loopDstX >> 1) & 3) + ((loopDstX >> 3) << 5) + (((loopDstY >> 3) * multiplierDstY) << 5) + ((u32)(loopDstY << 29) >> 27);
                toOrr = (*pixelsSrc >> ((loopSrcX & 1) * 4)) & 0xF;
                if (toOrr != colorKey)
                {
                    toShift = (loopDstX & 1) * 4;
                    *pixelsDst = (toOrr << toShift) | (*pixelsDst & (0xF0 >> toShift));
                }
            }
        }
    }
}

void FillBitmapRect4Bit(struct Bitmap *surface, u16 x, u16 y, u16 width, u16 height, u8 fillValue)
{
    s32 xEnd;
    s32 yEnd;
    s32 multiplierY;
    s32 loopX, loopY;
    u8 toOrr1, toOrr2;

    xEnd = x + width;
    if (xEnd > surface->width)
        xEnd = surface->width;

    yEnd = y + height;
    if (yEnd > surface->height)
        yEnd = surface->height;

    multiplierY = (surface->width + (surface->width & 7)) >> 3;
    toOrr1 = fillValue << 4;
    toOrr2 = fillValue & 0xF;

    for (loopY = y; loopY < yEnd; loopY++)
    {
        for (loopX = x; loopX < xEnd; loopX++)
        {
            u8 *pixels = surface->pixels + ((loopX >> 1) & 3) + ((loopX >> 3) << 5) + (((loopY >> 3) * multiplierY) << 5) + ((u32)(loopY << 29) >> 27);
            if ((loopX & 1))
                *pixels = (*pixels & 0xF) | toOrr1;
            else
                *pixels = (*pixels & 0xF0) | toOrr2;
        }
    }
}

void BlitBitmapRect4BitTo8Bit(const struct Bitmap *src, struct Bitmap *dst, u16 srcX, u16 srcY, u16 dstX, u16 dstY, u16 width, u16 height, u8 colorKey, u8 paletteOffset)
{
    s32 xEnd;
    s32 yEnd;
    s32 multiplierSrcY;
    s32 multiplierDstY;
    s32 loopSrcY, loopDstY;
    s32 loopSrcX, loopDstX;
    const u8 *pixelsSrc;
    u8 *pixelsDst;
    u8 colorKeyBits;

    paletteOffset <<= 4;
    colorKeyBits = colorKey << 4;

    if (dst->width - dstX < width)
        xEnd = (dst->width - dstX) + srcX;
    else
        xEnd = width + srcX;

    if (dst->height - dstY < height)
        yEnd = (srcY + dst->height) - dstY;
    else
        yEnd = srcY + height;

    multiplierSrcY = (src->width + (src->width & 7)) >> 3;
    multiplierDstY = (dst->width + (dst->width & 7)) >> 3;

    if (colorKey == 0xFF)
    {
        for (loopSrcY = srcY, loopDstY = dstY; loopSrcY < yEnd; loopSrcY++, loopDstY++)
        {
            pixelsSrc = src->pixels + ((srcX >> 1) & 3) + ((srcX >> 3) << 5) + (((loopSrcY >> 3) * multiplierSrcY) << 5) + ((u32)(loopSrcY << 29) >> 27);
            for (loopSrcX = srcX, loopDstX = dstX; loopSrcX < xEnd; loopSrcX++, loopDstX++)
            {
                pixelsDst = dst->pixels + (loopDstX & 7) + ((loopDstX >> 3) << 6) + (((loopDstY >> 3) * multiplierDstY) << 6) + ((u32)(loopDstY << 29) >> 26);
                if (loopSrcX & 1)
                {
                    *pixelsDst = paletteOffset + (*pixelsSrc >> 4);
                }
                else
                {
                    pixelsSrc = src->pixels + ((loopSrcX >> 1) & 3) + ((loopSrcX >> 3) << 5) + (((loopSrcY >> 3) * multiplierSrcY) << 5) + ((u32)(loopSrcY << 29) >> 27);
                    *pixelsDst = paletteOffset + (*pixelsSrc & 0xF);
                }
            }
        }
    }
    else
    {
        for (loopSrcY = srcY, loopDstY = dstY; loopSrcY < yEnd; loopSrcY++, loopDstY++)
        {
            pixelsSrc = src->pixels + ((srcX >> 1) & 3) + ((srcX >> 3) << 5) + (((loopSrcY >> 3) * multiplierSrcY) << 5) + ((u32)(loopSrcY << 29) >> 27);
            for (loopSrcX = srcX, loopDstX = dstX; loopSrcX < xEnd; loopSrcX++, loopDstX++)
            {
                if (loopSrcX & 1)
                {
                    if ((*pixelsSrc & 0xF0) != colorKeyBits)
                    {
                        pixelsDst = dst->pixels + (loopDstX & 7) + ((loopDstX >> 3) << 6) + (((loopDstY >> 3) * multiplierDstY) << 6) + ((u32)(loopDstY << 29) >> 26);
                        *pixelsDst = paletteOffset + (*pixelsSrc >> 4);
                    }
                }
                else
                {
                    pixelsSrc = src->pixels + ((loopSrcX >> 1) & 3) + ((loopSrcX >> 3) << 5) + (((loopSrcY >> 3) * multiplierSrcY) << 5) + ((u32)(loopSrcY << 29) >> 27);
                    if ((*pixelsSrc & 0xF) != colorKey)
                    {
                        pixelsDst = dst->pixels + (loopDstX & 7) + ((loopDstX >> 3) << 6) + (((loopDstY >> 3) * multiplierDstY) << 6) + ((u32)(loopDstY << 29) >> 26);
                        *pixelsDst = paletteOffset + (*pixelsSrc & 0xF);
                    }
                }
            }
        }
    }
}

void FillBitmapRect8Bit(struct Bitmap *surface, u16 x, u16 y, u16 width, u16 height, u8 fillValue)
{
    s32 xEnd;
    s32 yEnd;
    s32 multiplierY;
    s32 loopX, loopY;

    xEnd = x + width;
    if (xEnd > surface->width)
        xEnd = surface->width;

    yEnd = y + height;
    if (yEnd > surface->height)
        yEnd = surface->height;

    multiplierY = (surface->width + (surface->width & 7)) >> 3;

    for (loopY = y; loopY < yEnd; loopY++)
    {
        for (loopX = x; loopX < xEnd; loopX++)
        {
            u8 *pixels = surface->pixels + (loopX & 7) + ((loopX >> 3) << 6) + (((loopY >> 3) * multiplierY) << 6) + ((u32)(loopY << 29) >> 26);
            *pixels = fillValue;
        }
    }
}
