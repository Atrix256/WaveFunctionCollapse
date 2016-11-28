#define _CRT_SECURE_NO_WARNINGS
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                BMP LOADING AND SAVING
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

enum class EPalletIndex : uint64 { e_undecided = (uint64)-1 };

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

typedef std::vector<EPalletIndex> TPattern;

struct SPattern
{
	TPattern	m_pattern;
	uint64		m_count;
};

typedef std::vector<SPattern> TPatternList;

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

void GetPatterns (const SPalletizedImageData& palletizedImage, TPatternList& patterns, bool periodicInput, size_t tileSize, size_t symmetry)
{
	TPattern srcPattern;
	TPattern tmpPattern;
	srcPattern.resize(tileSize*tileSize);
	tmpPattern.resize(tileSize*tileSize);

	size_t maxX = palletizedImage.m_width - (periodicInput ? tileSize : 0);
	size_t maxY = palletizedImage.m_height - (periodicInput ? tileSize : 0);
	for (size_t y = 0; y < maxY; ++y)
	{
		for (size_t x = 0; x < maxX; ++x)
		{
			// get and add the pattern
			GetPattern(palletizedImage, x, y, tileSize, srcPattern);
			AddPattern(patterns, srcPattern);

			// add rotations and reflections, as instructed by symmetry parameter
			for (size_t i = 1; i < symmetry; ++i)
			{
				if (i % 2 == 1)
				{
					ReflectPatternXAxis(srcPattern, tmpPattern, tileSize);
					AddPattern(patterns, srcPattern);
				}
				else
				{
					RotatePatternCW90(srcPattern, tmpPattern, tileSize);
					AddPattern(patterns, srcPattern);
					srcPattern = tmpPattern;
				}
			}
		}
	}
}

void SavePatterns (const TPatternList& patterns, const char* srcFileName, size_t tileSize, size_t bpp, const std::vector<SPixel>& pallete)
{
    SImageData tempImageData;
    tempImageData.m_width = tileSize;
    tempImageData.m_height = tileSize;
    tempImageData.m_pitch = tileSize * 3;
    if (tempImageData.m_pitch & 3)
    {
        tempImageData.m_pitch &= ~3;
        tempImageData.m_pitch += 4;
    }
    tempImageData.m_pixels.resize(tempImageData.m_pitch*tempImageData.m_height);
	uint64 patternIndex = 0;
	for (const SPattern& pattern : patterns)
    {
		const EPalletIndex* srcPixel = &pattern.m_pattern[0];
        for (size_t y = 0; y < tileSize; ++y)
        {
            for (size_t x = 0; x < tileSize; ++x)
            {
                *(SPixel*)&tempImageData.m_pixels[y * tempImageData.m_pitch + x * 3] = pallete[(size_t)*srcPixel];
				++srcPixel;
            }
        }

        char buffer[256];
        sprintf(buffer, ".Pattern%I64i.%I64i.bmp", patternIndex, pattern.m_count);

        char fileName[256];
        strcpy(fileName, srcFileName);
        strcat(fileName, buffer);

        SaveImage(fileName, tempImageData);

		++patternIndex;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                UNORGANIZED
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef std::vector<bool>			TSuperpositionalPixels;
typedef std::vector<EPalletIndex>	TObservedColors;

enum class EObserveResult {
	e_success,
	e_failure,
	e_notDone
};

uint64 CountPixelPossibilities (TSuperpositionalPixels& superPositionalPixels, size_t pixelBoolIndex, const TPatternList& patterns, size_t tileSize)
{
	// Count how many possibilities there are
	uint64 possiblePatternCount = 0;
	const size_t tileSizeSq = tileSize * tileSize;
	size_t patternPositionOffset = 0;
	for (size_t patternIndex = 0, patternCount = patterns.size(); patternIndex < patternCount; ++patternIndex)
	{
		for (size_t positionIndex = 0, positionCount = tileSizeSq; positionIndex < positionCount; ++positionIndex, ++patternPositionOffset)
		{
			if (!superPositionalPixels[pixelBoolIndex + patternPositionOffset])
				continue;

			possiblePatternCount += patterns[patternIndex].m_count;
		}
	}

	return possiblePatternCount;
}

EObserveResult Observe (size_t width, size_t height, TObservedColors& observedColors, std::vector<bool>& changedPixels, TSuperpositionalPixels& superPositionalPixels, const TPatternList& patterns, size_t boolsPerPixel, size_t tileSize, size_t& undecidedPixels, SPRNG& prng)
{
	// Find the pixel with the smallest entropy (uncertainty), by finding the pixel with the smallest number of possibilities, which isn't yet observed/decided
	size_t minPixelX = -1;
    size_t minPixelY = -1;
	uint64 minPossibilities = (uint64)-1;
	size_t pixelIndex = 0;
	for (size_t y = 0; y < height; ++y)
	{
		for (size_t x = 0; x < width; ++x, ++pixelIndex)
		{
			// skip pixels which are already decided
			if (observedColors[pixelIndex] != EPalletIndex::e_undecided)
				continue;
			++undecidedPixels;

			size_t boolIndex = pixelIndex * boolsPerPixel;
			uint64 possibilities = CountPixelPossibilities(superPositionalPixels, boolIndex, patterns, tileSize);

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
	uint64 selectedPossibility = prng.RandomInt<uint64>(0, minPossibilities);
	size_t patternPositionOffset = 0;
	const size_t tileSizeSq = tileSize * tileSize;
	pixelIndex = minPixelY * width + minPixelX;
	size_t boolIndex = pixelIndex * boolsPerPixel;
	for (size_t patternIndex = 0, patternCount = patterns.size(); patternIndex < patternCount; ++patternIndex)
	{
		const size_t currentPatternCount = patterns[patternIndex].m_count;
		for (size_t positionIndex = 0, positionCount = tileSizeSq; positionIndex < positionCount; ++positionIndex, ++patternPositionOffset)
		{
			if (!superPositionalPixels[boolIndex + patternPositionOffset])
				continue;

			// if this is NOT the selected pattern, mark it as not possible
			if (selectedPossibility == (uint64)-1 || selectedPossibility > currentPatternCount)
			{
				superPositionalPixels[boolIndex + patternPositionOffset] = false;
				selectedPossibility -= currentPatternCount;
			}
			// else it IS the selected pattern, leave it as possible, and set the observed color
			else
			{
				selectedPossibility = (uint64)-1;
				observedColors[pixelIndex] = patterns[patternIndex].m_pattern[positionIndex];
			}
		}
	}

	// mark this pixel as changed so that Propogate() knows to propagate it's changes
	changedPixels[pixelIndex] = true;

	// return that we still have more work to do
	return EObserveResult::e_notDone;	
}

void PropagatePatternRestrictions (size_t changedPixelX, size_t changedPixelY, size_t affectedPixelX, size_t affectedPixelY, const TPatternList& patterns, TSuperpositionalPixels& superPositionalPixels, size_t boolsPerPixel, size_t imageWidth, size_t imageHeight, size_t tileSize, int patternOffsetX, int patternOffsetY)
{
    // TODO: remove this comment after things are working
    // 1) loop through all patterns that are possible in the affectedPixel...
    // 2) loop through all patterns that are possible in the changedPixel...
    // 3) Mark as impossible any pattern in affectedPixel that don't match a possible pattern in changedPixel, taking pixel position offsets into account!

    
    // If any possible pattern in the affectedPixel doesn't match a possible pattern in changedPixel, mark it as impossible.
    // Note that we need to take into account the offset between the pixels, and only care about locations that are inside both patterns.
    size_t changedPixelIndex = changedPixelY * imageWidth + changedPixelX;
    size_t affectedPixelIndex = affectedPixelY * imageWidth + affectedPixelX;

    size_t changedPixelBoolIndex = changedPixelIndex * boolsPerPixel;
    size_t affectedPixelBoolIndex = affectedPixelIndex * boolsPerPixel;

    const size_t positionCount = tileSize * tileSize;

    // Loop through the affectedPixel possible patterns to see if any are made impossible by the changed pixel's constraints
    for (size_t affectedPixelOffset = 0; affectedPixelOffset < boolsPerPixel; ++affectedPixelOffset)
    {
        if (!superPositionalPixels[affectedPixelBoolIndex + affectedPixelOffset])
            continue;

        size_t affectedPatternIndex = affectedPixelOffset / positionCount;
        size_t affectedPatternOffsetPixelIndex = affectedPixelOffset % positionCount;

        size_t affectedPatternOffsetPixelX = affectedPatternOffsetPixelIndex % tileSize;
        size_t affectedPatternOffsetPixelY = affectedPatternOffsetPixelIndex / tileSize;

        const SPattern& currentAffectedPixelPattern = patterns[affectedPatternIndex];

        // Loop through the changedPixel possible patterns to see if any match the offset affectedPixel patterns
        bool patternOK = false;
        for (size_t changedPixelOffset = 0; changedPixelOffset < boolsPerPixel && !patternOK; ++changedPixelOffset)
        {
            if (!superPositionalPixels[changedPixelBoolIndex + changedPixelOffset])
                continue;

            size_t changedPatternIndex = changedPixelOffset / positionCount;
            size_t changedPatternOffsetPixelIndex = changedPixelOffset % positionCount;

            size_t changedPatternOffsetPixelX = changedPatternOffsetPixelIndex % tileSize;
            size_t changedPatternOffsetPixelY = changedPatternOffsetPixelIndex / tileSize;

            const SPattern& currentChangedPixelPattern = patterns[changedPatternIndex];

            // calculate how much to offset the currentChangedPixelPattern to line it up with the currentAffectedPixelPattern
            int offsetX = patternOffsetX - (int)affectedPatternOffsetPixelX + (int)changedPatternOffsetPixelX;
            int offsetY = patternOffsetY - (int)affectedPatternOffsetPixelY + (int)changedPatternOffsetPixelY;

            int ijkl = 0;

            // TODO: is the offset calculated correctly?
            // TODO: patternOK = patternOK || PatternMatches(affectedPixelPattern, changedPixelPattern, patternOffsetX, patternOffsetY) 
        }

        // if the pattern is ok, nothing else to do!
        if (patternOK)
            continue;

        // otherwise, disable this pattern and remember that we've changed this affectedPixel
        superPositionalPixels[affectedPixelBoolIndex + affectedPixelOffset] = patternOK;
        // TODO: mark affectedPixel as changed!
    }   

    // TODO: should the above pattern of single for loop be used for other places that interact with superPositionalPixels?

    int ijkl = 0;
}

bool Propagate (size_t width, size_t height, size_t tileSize, const TPatternList& patterns, std::vector<bool>& changedPixels, TSuperpositionalPixels& superPositionalPixels, size_t boolsPerPixel, size_t imageWidth, size_t imageHeight)
{
	// find a changed pixel.  If none found, return false. Else, mark the pixel as unchanged since we will handle it.
	size_t i = 0;
	size_t c = changedPixels.size();
	while (i < c && !changedPixels[i])
		++i;
	if (i >= c)
		return false;
	changedPixels[i] = false;

	// TODO: handle this changed pixel.
	// * This pixel now has a definite color.
	// * For all pixels possibly affected by this change...
	//  * Mark as false any pattern possibilities which don't have this color for this pixel?
	//  * It may actually be more complex than that.  The original impl makes it so the patterns have to match. Not sure if really required?!
	//  * Actually yeah... this pixel may not have a definite color, but it does have at least one pattern which is not allowed. So, need to work in patterns, not pixel colors.

	// Process all pixels that could be affected by a change to this pixel
	size_t changedPixelX = i % width;
	size_t changedPixelY = i / width;
	for (int indexY = -(int)tileSize + 1, stopY = (int)tileSize; indexY < stopY; ++indexY)
	{
		for (int indexX = -(int)tileSize + 1, stopX = (int)tileSize; indexX < stopX; ++indexX)
		{
			size_t affectedPixelX = (changedPixelX + indexX + width) % width;
			size_t affectedPixelY = (changedPixelY + indexY + height) % height;
            PropagatePatternRestrictions(changedPixelX, changedPixelY, affectedPixelX, affectedPixelY, patterns, superPositionalPixels, boolsPerPixel, imageWidth, imageHeight, tileSize, indexX, indexY);
		}
	}

	// return that we did do some work
	return true;
}

void PropagateAllChanges (size_t width, size_t height, size_t tileSize, const TPatternList& patterns, std::vector<bool>& changedPixels, TSuperpositionalPixels& superPositionalPixels, size_t boolsPerPixel, size_t imageWidth, size_t imageHeight)
{
	// Propagate until no progress can be made
	while (Propagate(width, height, tileSize, patterns, changedPixels, superPositionalPixels, boolsPerPixel, imageWidth, imageHeight));
}

void SaveFinalImage(const char* srcFileName, size_t width, size_t height, const TObservedColors& observedColors, const SPalletizedImageData& palletizedImage)
{
	// allocate space for the image
	SImageData tempImageData;
	tempImageData.m_width = width;
	tempImageData.m_height = height;
	tempImageData.m_pitch = width * 3;
	if (tempImageData.m_pitch & 3)
	{
		tempImageData.m_pitch &= ~3;
		tempImageData.m_pitch += 4;
	}
	tempImageData.m_pixels.resize(tempImageData.m_pitch*tempImageData.m_height);

	// set the output image pixels , based on the observed colors
	const EPalletIndex* srcPixel = &observedColors[0];
	for (size_t y = 0; y < height; ++y)
	{
		SPixel* destPixel = (SPixel*)&tempImageData.m_pixels[y*tempImageData.m_pitch];
		for (size_t x = 0; x < width; ++x, ++destPixel, ++srcPixel)
		{
			*destPixel = palletizedImage.m_pallete[(size_t)*srcPixel];
		}
	}

	// write the file
	char fileName[256];
	strcpy(fileName, srcFileName);
	strcat(fileName, ".out.bmp");
	SaveImage(fileName, tempImageData);
}


int main(int argc, char **argv)
{
    // Parameters
    const size_t c_tileSize = 3;
    const char* c_fileName = "Samples\\Knot.bmp";
    bool periodicInput = true;
    bool periodicOutput = true;
    size_t symmetry = 8;
	size_t outputImageWidth = 48;
	size_t outputImageHeight = 48;
	uint32 prngSeed = -1;

    // initialize random number generator
    SPRNG prng(prngSeed);

    // Load image
    SImageData colorImage;
    if (!LoadImage(c_fileName, colorImage)) {
        fprintf(stderr, "Could not load image: %s\n", c_fileName);
        return 1;
    }

    // Palletize the image for simpler processing of pixels
    SPalletizedImageData palletizedImage;
    PalletizeImage(colorImage, palletizedImage);

    // Gather the patterns from the source data
    TPatternList patterns;
    GetPatterns(palletizedImage, patterns, periodicInput, c_tileSize, symmetry);

	// initialize our superpositional pixel information which describes which patterns in what positions each pixel has as a possibility
	size_t numPixels = outputImageWidth * outputImageHeight;
	size_t boolsPerPixel = patterns.size() * c_tileSize * c_tileSize;
	TSuperpositionalPixels superPositionalPixels;
	superPositionalPixels.resize(numPixels*boolsPerPixel, true);

	// initialize our observed colors for each pixel, which starts out as undecided
	TObservedColors observedColors;
	observedColors.resize(numPixels, EPalletIndex::e_undecided);

	// initialize which pixels have been changed - starting with none (all false)
	std::vector<bool> changedPixels;
	changedPixels.resize(numPixels, false);

	// Do wave collapse
	EObserveResult observeResult = EObserveResult::e_notDone;
    uint32 lastPercent = 0;
    printf("Progress: 0%%");
	while (1)
	{
		size_t undecidedPixels = 0;
		observeResult = Observe(outputImageWidth, outputImageHeight, observedColors, changedPixels, superPositionalPixels, patterns, boolsPerPixel, c_tileSize, undecidedPixels, prng);
		if (observeResult != EObserveResult::e_notDone)
			break;

        uint32 percent = 100 - uint32(100.0f * float(undecidedPixels) / (float(outputImageWidth)*float(outputImageHeight)));
        
        if (lastPercent != percent)
        {
            printf("\rProgress: %i%%", percent);
            lastPercent = percent;
        }

		PropagateAllChanges(outputImageWidth, outputImageHeight, c_tileSize, patterns, changedPixels, superPositionalPixels, boolsPerPixel, outputImageWidth, outputImageHeight);
	}

    // Save the final image
	SaveFinalImage(c_fileName, outputImageWidth, outputImageHeight, observedColors, palletizedImage);
	return 0;
}

/*

TODO:

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