#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>  // for bitmap headers.  Sorry non windows people!

#include <vector>
#include <algorithm>
#include <stdint.h>
#include <unordered_set>

typedef uint8_t uint8;
typedef uint32_t uint32;
typedef uint64_t uint64;

struct SPixel
{
	uint8 B;
	uint8 G;
	uint8 R;
};

struct SImageData
{
	SImageData ()
		: m_width(0)
		, m_height(0)
	{ }

	size_t m_width;
	size_t m_height;
	size_t m_pitch;
	std::vector<uint8> m_pixels;
};

struct SPalletizedImageData
{
	SPalletizedImageData ()
		: m_width(0)
		, m_height(0)
	{ }

	size_t m_width;
	size_t m_height;
	size_t m_bpp; // bits per pixel. Based on number of items in pallete
	std::vector<size_t> m_pixels;
	std::vector<SPixel> m_pallete;
};

bool operator == (const SPixel& a, const SPixel& b)
{
	return a.B == b.B && a.G == b.G && a.R == b.R;
}

bool LoadImage (const char *fileName, SImageData& imageData)
{
    // open the file if we can
    FILE *file;
    file = fopen(fileName, "rb");
    if (!file)
        return false;
 
    // read the headers if we can
    BITMAPFILEHEADER header;
    BITMAPINFOHEADER infoHeader;
    if (fread(&header, sizeof(header), 1, file) != 1 ||
        fread(&infoHeader, sizeof(infoHeader), 1, file) != 1 ||
        header.bfType != 0x4D42 || infoHeader.biBitCount != 24)
    {
        fclose(file);
        return false;
    }
 
    // read in our pixel data if we can. Note that it's in BGR order, and width is padded to the next power of 4
    imageData.m_pixels.resize(infoHeader.biSizeImage);
    fseek(file, header.bfOffBits, SEEK_SET);
    if (fread(&imageData.m_pixels[0], imageData.m_pixels.size(), 1, file) != 1)
    {
        fclose(file);
        return false;
    }
 
    imageData.m_width = infoHeader.biWidth;
    imageData.m_height = infoHeader.biHeight;
 
	// calculate pitch
    imageData.m_pitch = imageData.m_width*3;
    if (imageData.m_pitch & 3)
    {
        imageData.m_pitch &= ~3;
        imageData.m_pitch += 4;
    }
 
    fclose(file);
    return true;
}
 
bool SaveImage (const char *fileName, const SImageData &image)
{
    // open the file if we can
    FILE *file;
    file = fopen(fileName, "wb");
    if (!file)
        return false;
 
    // make the header info
    BITMAPFILEHEADER header;
    BITMAPINFOHEADER infoHeader;
 
    header.bfType = 0x4D42;
    header.bfReserved1 = 0;
    header.bfReserved2 = 0;
    header.bfOffBits = 54;
 
    infoHeader.biSize = 40;
    infoHeader.biWidth = (long)image.m_width;
    infoHeader.biHeight = (long)image.m_height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;
    infoHeader.biCompression = 0;
    infoHeader.biSizeImage = (DWORD)image.m_pixels.size();
    infoHeader.biXPelsPerMeter = 0;
    infoHeader.biYPelsPerMeter = 0;
    infoHeader.biClrUsed = 0;
    infoHeader.biClrImportant = 0;
 
    header.bfSize = infoHeader.biSizeImage + header.bfOffBits;
 
    // write the data and close the file
    fwrite(&header, sizeof(header), 1, file);
    fwrite(&infoHeader, sizeof(infoHeader), 1, file);
    fwrite(&image.m_pixels[0], infoHeader.biSizeImage, 1, file);
    fclose(file);
    return true;
}

size_t GetOrMakePalleteIndex (SPalletizedImageData& palletizedImage, const SPixel& pixel)
{
	// see if this pixel value alread exists in the pallete
	auto it = std::find(palletizedImage.m_pallete.begin(), palletizedImage.m_pallete.end(), pixel);

	// if it was found, return it's index
	if (it != palletizedImage.m_pallete.end())
		return it - palletizedImage.m_pallete.begin();

	// else add it
	palletizedImage.m_pallete.push_back(pixel);
	return palletizedImage.m_pallete.size()-1;
}

void PalletizeImageRow (const SImageData& colorImage, SPalletizedImageData& palletizedImage, size_t y)
{
	// get source and dest pointers for this row
	const SPixel* srcPixel = (SPixel*)&colorImage.m_pixels[y * colorImage.m_pitch];
	size_t* destPixel = &palletizedImage.m_pixels[y * palletizedImage.m_width];

	// set the palletized pixel index to be the pallet index for the source pixel color
	for (size_t x = 0; x < colorImage.m_width; ++x, ++srcPixel, ++destPixel)
		*destPixel = GetOrMakePalleteIndex(palletizedImage, *srcPixel);
}

void PalletizeImage (const SImageData& colorImage, SPalletizedImageData& palletizedImage)
{
	// copy properties of color image to palletized image
	palletizedImage.m_width = colorImage.m_width;
	palletizedImage.m_height = colorImage.m_height;
	palletizedImage.m_pixels.resize(palletizedImage.m_width*palletizedImage.m_height);

	// process a row of data
	for (size_t y = 0; y < colorImage.m_height; ++y)
		PalletizeImageRow(colorImage, palletizedImage, y);

	// calculate bits per pixel
	size_t maxValue = 2;
	palletizedImage.m_bpp = 1;
	while (maxValue < palletizedImage.m_pallete.size())
	{
		maxValue *= 2;
		++palletizedImage.m_bpp;
	}
}

uint64 GetPattern (const SPalletizedImageData& palletizedImage, size_t startX, size_t startY, size_t tileSize)
{
	uint64 pattern = 0;

	// TODO: verify that the pattern gathering code is correct

	for (size_t iy = 0; iy < tileSize; ++iy)
	{
		size_t y = (startY + iy) % palletizedImage.m_height;

		for (size_t ix = 0; ix < tileSize; ++ix)
		{
			size_t x = (startX + ix) % palletizedImage.m_width;

			pattern = pattern << palletizedImage.m_bpp;
			pattern |= palletizedImage.m_pixels[(y*palletizedImage.m_width + x) * 3];
		}
	}

	return pattern;
}

void GatherUniquePatterns (const SPalletizedImageData& palletizedImage, bool periodicInput, size_t tileSize)
{
	size_t maxX = palletizedImage.m_width - (periodicInput ? tileSize : 0);
	size_t maxY = palletizedImage.m_height - (periodicInput ? tileSize : 0);

	std::unordered_set<uint64> patterns;

	for (size_t y = 0; y < maxY; ++y)
	{
		for (size_t x = 0; x < maxX; ++x)
		{
			uint64 pattern = GetPattern(palletizedImage, x, y, tileSize);
			patterns.insert(pattern);
		}
	}

	// TODO: gather the unique patterns
	// TODO: rotations and reflections (symmetry parameter?)
}

int main (int argc, char **argv)
{
	// Parameters
	const size_t c_tileSize = 3;
	const char* c_fileName = "Samples\\Knot.bmp";
	bool periodicInput = true;
	bool periodicOutput = true;

	// Load image
	SImageData colorImage;
	if (!LoadImage(c_fileName, colorImage)) {
		fprintf(stderr, "Could not load image: %s\n", c_fileName);
		return 1;
	}

	// Palletize the image for simpler processing of pixels
	SPalletizedImageData palletizedImage;
	PalletizeImage(colorImage, palletizedImage);

	// Gather the patterns
	GatherUniquePatterns(palletizedImage, periodicInput, c_tileSize);

	return 0;
}

/*

TODO:

* make this OOP?
* make parameters come from command line
* test! Maybe do all the tests from that XML file
* profile and optimize?
 * maybe keep this one clean for blog post and make another one that runs faster?
 * could also save that for a future impl when doing more realistic images

*/