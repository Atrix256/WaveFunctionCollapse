#define _CRT_SECURE_NO_WARNINGS
#define _HAS_ITERATOR_DEBUGGING 0 // stl is slow in debug!

#include <windows.h>  // for bitmap headers.  Sorry non windows people!
#undef min
#undef max

#include <vector>
#include <algorithm>
#include <stdint.h>
#include <random>

typedef uint8_t uint8;
typedef uint32_t uint32;
typedef uint64_t uint64;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                      MISC
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: could fold the PRNG into context.
// TODO: we don't need RandomDistribution anymore!
struct SPRNG
{
    SPRNG (uint32 seed = -1)
    {
        static std::random_device rd;
        m_rng.seed(seed == -1 ? rd() : seed);
    }

    template <typename T>
    T RandomInt (T min = std::numeric_limits<T>::min(), T max = std::numeric_limits<T>::max())
    {
        static std::uniform_int<T> dist(min, max);
        return dist(m_rng);
    }

    template <typename T>
    T RandomDistribution (const std::discrete_distribution<T>& distribution)
    {
        return distribution(m_rng);
    }

private:
    uint32          m_seed;
    std::mt19937    m_rng;
};

struct SPixel
{
	uint8 B;
	uint8 G;
	uint8 R;
};

bool operator == (const SPixel& a, const SPixel& b)
{
	return a.B == b.B && a.G == b.G && a.R == b.R;
}

enum class EPalletIndex : uint64 { e_undecided = (uint64)-1 };

struct SObservedPixel
{
	EPalletIndex	m_observedColor;
	size_t			m_patternIndex;
	size_t			m_positionIndex;
};

typedef std::vector<bool>			TSuperpositionalPixels;
typedef std::vector<SObservedPixel>	TObservedPixels;

struct SPalletizedImageData
{
	SPalletizedImageData()
		: m_width(0)
		, m_height(0)
	{ }

	size_t m_width;
	size_t m_height;
	size_t m_bpp;
	std::vector<EPalletIndex> m_pixels;
	std::vector<SPixel> m_pallete;
};

struct SImageData
{
	SImageData()
		: m_width(0)
		, m_height(0)
	{ }

	size_t m_width;
	size_t m_height;
	size_t m_pitch;
	std::vector<uint8> m_pixels;
};

typedef std::vector<EPalletIndex> TPattern;

struct SPattern
{
	TPattern	m_pattern;
	uint64		m_count;
};

typedef std::vector<SPattern> TPatternList;

struct SContext
{
	SContext(uint32 prngSeed = -1)
		: m_prng(prngSeed)
	{ }
	SPRNG		m_prng;

	SImageData				m_colorImage;
	SPalletizedImageData	m_palletizedImage;

	TPatternList			m_patterns;

	std::vector<bool>		m_changedPixels;

	TSuperpositionalPixels	m_superPositionalPixels;

	TObservedPixels			m_observedPixels;

	size_t		m_tileSize;
	const char* m_fileName;
	bool		m_periodicInput;
	bool		m_periodicOutput;
	uint8		m_symmetry;
	size_t		m_outputImageWidth;
	size_t		m_outputImageHeight;
	size_t		m_numPixels;
	size_t		m_boolsPerPixel;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                BMP LOADING AND SAVING
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                IMAGE PALLETIZATION
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

EPalletIndex GetOrMakePalleteIndex (SPalletizedImageData& palletizedImage, const SPixel& pixel)
{
	// see if this pixel value already exists in the pallete
	auto it = std::find(palletizedImage.m_pallete.begin(), palletizedImage.m_pallete.end(), pixel);

	// if it was found, return it's index
	if (it != palletizedImage.m_pallete.end())
		return (EPalletIndex)(it - palletizedImage.m_pallete.begin());

	// else add it
	palletizedImage.m_pallete.push_back(pixel);
	return (EPalletIndex)(palletizedImage.m_pallete.size()-1);
}

void PalletizeImageRow (const SImageData& colorImage, SPalletizedImageData& palletizedImage, size_t y)
{
	// get source and dest pointers for this row
	const SPixel* srcPixel = (SPixel*)&colorImage.m_pixels[y * colorImage.m_pitch];
	EPalletIndex* destPixel = &palletizedImage.m_pixels[y * palletizedImage.m_width];

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                PATTERN GATHERING
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void GetPattern (const SPalletizedImageData& palletizedImage, size_t startX, size_t startY, size_t tileSize, TPattern& outPattern)
{
	EPalletIndex* outPixel = &outPattern[0];
	for (size_t iy = 0; iy < tileSize; ++iy)
	{
		size_t y = (startY + iy) % palletizedImage.m_height;
		for (size_t ix = 0; ix < tileSize; ++ix)
		{
			size_t x = (startX + ix) % palletizedImage.m_width;
			*outPixel = palletizedImage.m_pixels[y*palletizedImage.m_width + x];
			++outPixel;
		}
	}
}

void AddPattern (TPatternList& patterns, const TPattern& pattern)
{
	auto it = std::find_if(
		patterns.begin(),
		patterns.end(),
		[&] (const SPattern& listPattern)
		{
			return listPattern.m_pattern == pattern;
		}
	);

	if (it != patterns.end())
	{
		(*it).m_count++;
	}
	else
	{
		SPattern newPattern;
		newPattern.m_count = 1;
		newPattern.m_pattern = pattern;
		patterns.push_back(newPattern);
	}
}

void ReflectPatternXAxis (const TPattern& inPattern, TPattern& outPattern, size_t tileSize)
{
	for (size_t outY = 0; outY < tileSize; ++outY)
	{
		for (size_t outX = 0; outX < tileSize; ++outX)
		{
			size_t outIndex = outY * tileSize + outX;

			size_t inX = tileSize - 1 - outX;
			size_t inY = outY;
			size_t inIndex = inY * tileSize + inX;
			
			outPattern[outIndex] = inPattern[inIndex];
		}
	}
}

void RotatePatternCW90 (const TPattern& inPattern, TPattern& outPattern, size_t tileSize)
{
	for (size_t outY = 0; outY < tileSize; ++outY)
	{
		for (size_t outX = 0; outX < tileSize; ++outX)
		{
			size_t outIndex = outY * tileSize + outX;

            size_t inX = outY;
            size_t inY = tileSize - 1 - outX;
			size_t inIndex = inY * tileSize + inX;

			outPattern[outIndex] = inPattern[inIndex];
        }
    }
}

void GetPatterns (SContext& context)
{
	TPattern srcPattern;
	TPattern tmpPattern;
	srcPattern.resize(context.m_tileSize*context.m_tileSize);
	tmpPattern.resize(context.m_tileSize*context.m_tileSize);

	size_t maxX = context.m_palletizedImage.m_width - (context.m_periodicInput ? context.m_tileSize : 0);
	size_t maxY = context.m_palletizedImage.m_height - (context.m_periodicInput ? context.m_tileSize : 0);
	for (size_t y = 0; y < maxY; ++y)
	{
		for (size_t x = 0; x < maxX; ++x)
		{
			// get and add the pattern
			GetPattern(context.m_palletizedImage, x, y, context.m_tileSize, srcPattern);
			AddPattern(context.m_patterns, srcPattern);

			// add rotations and reflections, as instructed by symmetry parameter
			for (uint8 i = 1; i < context.m_symmetry; ++i)
			{
				if (i % 2 == 1)
				{
					ReflectPatternXAxis(srcPattern, tmpPattern, context.m_tileSize);
					AddPattern(context.m_patterns, srcPattern);
				}
				else
				{
					RotatePatternCW90(srcPattern, tmpPattern, context.m_tileSize);
					AddPattern(context.m_patterns, srcPattern);
					srcPattern = tmpPattern;
				}
			}
		}
	}
}

void SavePatterns (SContext& context)
{
	// TODO: make a function on SImageData to construct one by width / height only, and use that here and anywhere else needed.
    SImageData tempImageData;
    tempImageData.m_width = context.m_tileSize;
    tempImageData.m_height = context.m_tileSize;
    tempImageData.m_pitch = context.m_tileSize * 3;
    if (tempImageData.m_pitch & 3)
    {
        tempImageData.m_pitch &= ~3;
        tempImageData.m_pitch += 4;
    }
    tempImageData.m_pixels.resize(tempImageData.m_pitch*tempImageData.m_height);
	uint64 patternIndex = 0;
	for (const SPattern& pattern : context.m_patterns)
    {
		const EPalletIndex* srcPixel = &pattern.m_pattern[0];
        for (size_t y = 0; y < context.m_tileSize; ++y)
        {
            for (size_t x = 0; x < context.m_tileSize; ++x)
            {
                *(SPixel*)&tempImageData.m_pixels[y * tempImageData.m_pitch + x * 3] = context.m_palletizedImage.m_pallete[(size_t)*srcPixel];
				++srcPixel;
            }
        }

        char buffer[256];
        sprintf(buffer, ".Pattern%I64i.%I64i.bmp", patternIndex, pattern.m_count);

        char fileName[256];
        strcpy(fileName, context.m_fileName);
        strcat(fileName, buffer);

        SaveImage(fileName, tempImageData);

		++patternIndex;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                UNORGANIZED
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum class EObserveResult {
	e_success,
	e_failure,
	e_notDone
};

uint64 CountPixelPossibilities (SContext& context, size_t pixelBoolIndex)
{
	// Count how many possibilities there are
	uint64 possiblePatternCount = 0;
	const size_t tileSizeSq = context.m_tileSize * context.m_tileSize;
	size_t patternPositionOffset = 0;
	for (size_t patternIndex = 0, patternCount = context.m_patterns.size(); patternIndex < patternCount; ++patternIndex)
	{
		for (size_t positionIndex = 0, positionCount = tileSizeSq; positionIndex < positionCount; ++positionIndex, ++patternPositionOffset)
		{
			if (!context.m_superPositionalPixels[pixelBoolIndex + patternPositionOffset])
				continue;

			possiblePatternCount += context.m_patterns[patternIndex].m_count;
		}
	}

	return possiblePatternCount;
}

EObserveResult Observe (SContext& context, size_t& undecidedPixels)
{
	// Find the pixel with the smallest entropy (uncertainty), by finding the pixel with the smallest number of possibilities, which isn't yet observed/decided
	size_t minPixelX = -1;
    size_t minPixelY = -1;
	uint64 minPossibilities = (uint64)-1;
	size_t pixelIndex = 0;
	for (size_t y = 0; y < context.m_outputImageHeight; ++y)
	{
		for (size_t x = 0; x < context.m_outputImageWidth; ++x, ++pixelIndex)
		{
			// skip pixels which are already decided
			if (context.m_observedPixels[pixelIndex].m_observedColor != EPalletIndex::e_undecided)
				continue;
			++undecidedPixels;

			size_t boolIndex = pixelIndex * context.m_boolsPerPixel;
			uint64 possibilities = CountPixelPossibilities(context, boolIndex);

			// if no possibilities, this is an impossible pixel
			if (possibilities == 0)
                return EObserveResult::e_failure;

			// otherwise, remember the minimum one we found
            if (possibilities < minPossibilities)
            {
				minPossibilities = possibilities;
				minPixelX = x;
				minPixelY = y;
            }
		}
	}

    // if all pixels are decided (no entropy left in the image), return success
    if (minPossibilities == (uint64)-1)
        return EObserveResult::e_success;

	// otherwise, select a possibility for this pixel
	// TODO: make this a function.  ObservePixel()
	uint64 selectedPossibility = context.m_prng.RandomInt<uint64>(0, minPossibilities);
	size_t patternPositionOffset = 0;
	const size_t tileSizeSq = context.m_tileSize * context.m_tileSize;
	pixelIndex = minPixelY * context.m_outputImageWidth + minPixelX;
	size_t boolIndex = pixelIndex * context.m_boolsPerPixel;
	for (size_t patternIndex = 0, patternCount = context.m_patterns.size(); patternIndex < patternCount; ++patternIndex)
	{
		const size_t currentPatternCount = context.m_patterns[patternIndex].m_count;
		for (size_t positionIndex = 0, positionCount = tileSizeSq; positionIndex < positionCount; ++positionIndex, ++patternPositionOffset)
		{
			if (!context.m_superPositionalPixels[boolIndex + patternPositionOffset])
				continue;

			// if this is NOT the selected pattern, mark it as not possible
			if (selectedPossibility == (uint64)-1 || selectedPossibility > currentPatternCount)
			{
				context.m_superPositionalPixels[boolIndex + patternPositionOffset] = false;
				selectedPossibility -= currentPatternCount;
			}
			// else it IS the selected pattern, leave it as possible, and set the observed color
			else
			{
				selectedPossibility = (uint64)-1;
				context.m_observedPixels[pixelIndex].m_observedColor = context.m_patterns[patternIndex].m_pattern[positionIndex];
				context.m_observedPixels[pixelIndex].m_patternIndex = patternIndex;
				context.m_observedPixels[pixelIndex].m_positionIndex = positionIndex;
			}
		}
	}

	// mark this pixel as changed so that Propogate() knows to propagate it's changes
	context.m_changedPixels[pixelIndex] = true;

	// return that we still have more work to do
	return EObserveResult::e_notDone;	
}

bool PatternMatches (const TPattern& patternA, const TPattern& patternB, int patternAOffsetX, int patternAOffsetY, int patternBOffsetX, int patternBOffsetY, size_t tileSize)
{
    int blah = -(int)tileSize + 1;
    int blah2 = (int)tileSize;

    // TODO: could easily find the min/max x and y to iterate over here
	for (int y = -(int)tileSize+1; y < (int)tileSize; ++y)
	{
		int pay = y + patternAOffsetY;
		int pby = y + patternBOffsetY;
		if (pay < 0 || pby < 0 || pay >= tileSize || pby >= tileSize)
			continue;

		for (int x = -(int)tileSize + 1; x < (int)tileSize; ++x)
		{
			int pax = x + patternAOffsetX;
			int pbx = x + patternBOffsetX;
			if (pax < 0 || pbx < 0 || pax >= tileSize || pbx >= tileSize)
				continue;

			if (patternA[pay*tileSize + pax] != patternB[pby*tileSize + pbx])
				return false;
		}
	}
	return true;
}

void PropagatePatternRestrictions (SContext& context, size_t changedPixelX, size_t changedPixelY, size_t affectedPixelX, size_t affectedPixelY, int patternOffsetX, int patternOffsetY)
{
    // If any possible pattern in the affectedPixel doesn't match a possible pattern in changedPixel, mark it as impossible.
    // Note that we need to take into account the offset between the pixels, and only care about locations that are inside both patterns.
    size_t changedPixelIndex = changedPixelY * context.m_outputImageWidth + changedPixelX;
    size_t affectedPixelIndex = affectedPixelY * context.m_outputImageWidth + affectedPixelX;

    size_t changedPixelBoolIndex = changedPixelIndex * context.m_boolsPerPixel;
    size_t affectedPixelBoolIndex = affectedPixelIndex * context.m_boolsPerPixel;

    const size_t positionCount = context.m_tileSize * context.m_tileSize;

    // Loop through the affectedPixel possible patterns to see if any are made impossible by the changed pixel's constraints
    for (size_t affectedPixelOffset = 0; affectedPixelOffset < context.m_boolsPerPixel; ++affectedPixelOffset)
    {
        if (!context.m_superPositionalPixels[affectedPixelBoolIndex + affectedPixelOffset])
            continue;

        size_t affectedPatternIndex = affectedPixelOffset / positionCount;
        size_t affectedPatternOffsetPixelIndex = affectedPixelOffset % positionCount;

        size_t affectedPatternOffsetPixelX = affectedPatternOffsetPixelIndex % context.m_tileSize;
        size_t affectedPatternOffsetPixelY = affectedPatternOffsetPixelIndex / context.m_tileSize;

        const TPattern& currentAffectedPixelPattern = context.m_patterns[affectedPatternIndex].m_pattern;

        // Loop through the changedPixel possible patterns to see if any match the offset affectedPixel patterns
        bool patternOK = false;
        for (size_t changedPixelOffset = 0; changedPixelOffset < context.m_boolsPerPixel && !patternOK; ++changedPixelOffset)
        {
            if (!context.m_superPositionalPixels[changedPixelBoolIndex + changedPixelOffset])
                continue;

            size_t changedPatternIndex = changedPixelOffset / positionCount;
            size_t changedPatternOffsetPixelIndex = changedPixelOffset % positionCount;

            size_t changedPatternOffsetPixelX = changedPatternOffsetPixelIndex % context.m_tileSize;
            size_t changedPatternOffsetPixelY = changedPatternOffsetPixelIndex / context.m_tileSize;

            const TPattern& currentChangedPixelPattern = context.m_patterns[changedPatternIndex].m_pattern;

			// if we find a matching pattern, we can bail out
			patternOK = PatternMatches(currentAffectedPixelPattern, currentChangedPixelPattern, (int)affectedPatternOffsetPixelX, (int)affectedPatternOffsetPixelY, (int)changedPatternOffsetPixelX + patternOffsetX, (int)changedPatternOffsetPixelY + patternOffsetY, context.m_tileSize);
        }

        // if the pattern is ok, nothing else to do!
        if (patternOK)
            continue;

        // otherwise, disable this pattern and remember that we've changed this affectedPixel
		context.m_superPositionalPixels[affectedPixelBoolIndex + affectedPixelOffset] = patternOK;
		context.m_changedPixels[affectedPixelIndex] = true;
    }
}

bool Propagate (SContext& context)
{
	// find a changed pixel.  If none found, return false. Else, mark the pixel as unchanged since we will handle it.
	size_t i = 0;
	while (i < context.m_numPixels && !context.m_changedPixels[i])
		++i;
	if (i >= context.m_numPixels)
		return false;
	context.m_changedPixels[i] = false;

	// Process all pixels that could be affected by a change to this pixel
	size_t changedPixelX = i % context.m_outputImageWidth;
	size_t changedPixelY = i / context.m_outputImageWidth;
	for (int indexY = -(int)context.m_tileSize + 1, stopY = (int)context.m_tileSize; indexY < stopY; ++indexY)
	{
		for (int indexX = -(int)context.m_tileSize + 1, stopX = (int)context.m_tileSize; indexX < stopX; ++indexX)
		{
			size_t affectedPixelX = (changedPixelX + indexX + context.m_outputImageWidth) % context.m_outputImageWidth;
			size_t affectedPixelY = (changedPixelY + indexY + context.m_outputImageHeight) % context.m_outputImageHeight;
            PropagatePatternRestrictions(context, changedPixelX, changedPixelY, affectedPixelX, affectedPixelY, indexX, indexY);
		}
	}

	// return that we did do some work
	return true;
}

void PropagateAllChanges (SContext& context)
{
	// Propagate until no progress can be made
	while (Propagate(context));
}

void SaveFinalImage (SContext& context)
{
	// allocate space for the image
	SImageData tempImageData;
	tempImageData.m_width = context.m_outputImageWidth;
	tempImageData.m_height = context.m_outputImageHeight;
	tempImageData.m_pitch = context.m_outputImageWidth * 3;
	if (tempImageData.m_pitch & 3)
	{
		tempImageData.m_pitch &= ~3;
		tempImageData.m_pitch += 4;
	}
	tempImageData.m_pixels.resize(tempImageData.m_pitch*tempImageData.m_height);

	// set the output image pixels , based on the observed colors
	const SObservedPixel* srcPixel = &context.m_observedPixels[0];
	for (size_t y = 0; y < context.m_outputImageHeight; ++y)
	{
		SPixel* destPixel = (SPixel*)&tempImageData.m_pixels[y*tempImageData.m_pitch];
		for (size_t x = 0; x < context.m_outputImageWidth; ++x, ++destPixel, ++srcPixel)
		{
			// TODO: handle the srcPixel being undecided!
			*destPixel = context.m_palletizedImage.m_pallete[(size_t)srcPixel->m_observedColor];
		}
	}

	// write the file
	char fileName[256];
	strcpy(fileName, context.m_fileName);
	strcat(fileName, ".out.bmp");
	SaveImage(fileName, tempImageData);
}


int main(int argc, char **argv)
{
	// TODO: could move all this calculation stuff into a "run" function that takes the params as function params?

	// Parameters
	SContext context(0); // TODO: temp! remove this param so seed goes back to -1
	context.m_tileSize = 3;
	context.m_fileName = "Samples\\Knot.bmp";;
	context.m_periodicInput = true;
	context.m_periodicOutput = true;
	context.m_symmetry = 8;
	context.m_outputImageWidth = 6;
	context.m_outputImageHeight = 6;
	context.m_numPixels = context.m_outputImageWidth * context.m_outputImageHeight;

    // Load image
    if (!LoadImage(context.m_fileName, context.m_colorImage)) {
        fprintf(stderr, "Could not load image: %s\n", context.m_fileName);
        return 1;
    }

    // Palletize the image for simpler processing of pixels
    PalletizeImage(context.m_colorImage, context.m_palletizedImage);

    // Gather the patterns from the source data
    GetPatterns(context);

	context.m_boolsPerPixel = context.m_patterns.size() * context.m_tileSize * context.m_tileSize;

	// initialize our superpositional pixel information which describes which patterns in what positions each pixel has as a possibility
	// TODO: make this stuff happen in the context constructor
	context.m_superPositionalPixels.resize(context.m_numPixels*context.m_boolsPerPixel, true);

	// initialize our observed colors for each pixel, which starts out as undecided
	context.m_observedPixels.resize(context.m_numPixels, { EPalletIndex::e_undecided, (size_t)-1, (size_t)-1 });

	// initialize which pixels have been changed - starting with none (all false)
	context.m_changedPixels.resize(context.m_numPixels, false);

	// Uncomment to see the patterns found
	//SavePatterns(context);

	// Do wave collapse
	EObserveResult observeResult = EObserveResult::e_notDone;
    uint32 lastPercent = 0;
    printf("Progress: 0%%");
	while (1)
	{
		size_t undecidedPixels = 0;
		observeResult = Observe(context, undecidedPixels);
		if (observeResult != EObserveResult::e_notDone)
			break;

        uint32 percent = 100 - uint32(100.0f * float(undecidedPixels) / float(context.m_numPixels));
        
        if (lastPercent != percent)
        {
            printf("\rProgress: %i%%", percent);
            lastPercent = percent;
        }

		PropagateAllChanges(context);
	}

	if (observeResult == EObserveResult::e_success)
		printf("success");
	else
		printf("failure!");

    // Save the final image
	SaveFinalImage(context);
	return 0;
}

/*

TODO:

! need to simplify patterns and trace through small image to see what happens.  Maybe make it print out debug info?

* profile and see where the slow downs are, in case any easy fixes that don't complicate things.

* there are too many params passed around.  Make a class!
 * instead of making it all oop, maybe just have a context struct that has all the state in it.

* clean out unused stuff, after the new implementation is working

* print out timing and progress

? should we set a limit on how many iterations it can do?

* could make this oop - or should this stay as a single source file if possible?

! I really have no idea what the propagator array does.  It's 4d! [2*N-1][2*N-1][T][]

* make parameters come from command line?
 * could also just hard code it.  Maybe a macro list describing all the experiments?

* make params go into a special structure passed by const ref?

* make sure periodic input and periodic output params are honored

* support ground feature where there are specific pixels that are initialized and can't be changed.

* test! Maybe do all the tests from that XML file

* could break up the code into commented sections to help organize it, even though it's just a single file.

* profile and optimize?
 * maybe keep this one clean for blog post and make another one that runs faster?
 * could also save that for a future impl when doing more realistic images

* use enum classes to help with type safety. palette index type etc.
! anywhere you have a comment, see if you need to organize it better

* clean up code when it's working
* build w32 and x64 to make sure no warnings / errors

* error handling: like when N is too large, or too many colors to fit into uint64. maybe leave it alone and keep the code simple?

* print out seed value, so it can be re-run it needed

* make some structs with helper functions instead of just having std::set and std::vector of stuff. easier to read / less error prone.

? maybe we don't need to calculate entropy in log space.  If all we need is the minimum value, the unlogged values seem like they should work too.
 * could research, and also test with some seeds to see if it changes anything!

? what should we do (image save out?) when it fails to find a solution? ie an impossibility is hit.

* Notes:
 * The original code added noise to the entropy calculations to randomize it a bit.  The author said it made the animations more pleasing but wasn't sure if it made a difference to runtime.
 * could optimize, multithread, OOP.  Trying to focus on making a single cpp file that plainly describes things.

* Blog:
 * mention trade off of code:
  * when making something to show people you can either make code that's easy to read and slow, or optimized code that is hard to read.
  * The library code is meant to be somewhat functional, so there are some optimizations that obscure the functionality (like the propagator!)
  * this code is meant only to show you how it works, so is slow.
  * TODO: mention the O() complexity.  It's gotta be like n^8 or something :P
 * Note that the original added a little bit of noise to entropy, and that it made animations better, but unknown if it helped anything
 * My impl and orig have NxN tiles, but could also do NxM tiles if you wanted to.
 * Could weight tiles differently if you wanted.
 * Can use for interactive procedural content, or procedural generation with constraints.
   * human can pre-limit some possibilities, or put in some hard decisions, then let the algorithm run and fill in the details that aren't cared about.
   * could regenerate several different things from the same constraints.  To re-roll something if you don't like it.  Or, to make variety without changing things that you actually care about.

* Next:
 * simple tiled model
 * make some fast CPU version? multithreaded, focused on speed etc.
 * JFA?

*/