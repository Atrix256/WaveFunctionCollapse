#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>  // for bitmap headers.  Sorry non windows people!
#undef min
#undef max

#include <vector>
#include <algorithm>
#include <stdint.h>
#include <unordered_map>
#include <array>
#include <random>

typedef uint8_t uint8;
typedef uint32_t uint32;
typedef uint64_t uint64;

enum class EPattern : uint64 {};

enum class EObserveResult {
	e_success,
	e_failure,
	e_notDone
};

enum class EGetPixelEntropyResult
{
    e_OK,
    e_noPossibilities
};

// The key is the pattern converted to a uint64.
// The value is how many times that pattern appeared.
// TODO: i don't think we are calculating weight correctly.  If we rotate a pattern that has a weight of 8, and find it exists, we only add 1 to the found pattern, not 8!
typedef std::unordered_map<EPattern, uint64>        TPatternList;

typedef std::vector<struct SSuperpositionalPixel>   TSuperpositionalPixels;

struct SPixel
{
	uint8 B;
	uint8 G;
	uint8 R;
};

struct SSuperpositionalPixel
{
    SSuperpositionalPixel()
        : m_changed(false)
    { }

    std::vector<bool>   m_possiblePatterns;
    // TODO: should we have a changed list instead, so we don't need to search all pixels to find the ones that changed?
    // TODO: could have an open and closed list or similar as well
    bool                m_changed;
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
	size_t m_bpp;
	std::vector<size_t> m_pixels;
	std::vector<SPixel> m_pallete;
};

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
	// see if this pixel value already exists in the pallete
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

EPattern GetPattern (const SPalletizedImageData& palletizedImage, size_t startX, size_t startY, size_t tileSize)
{
    // convert a <tileSize> x <tileSize> pattern into a uint64 and return it
	uint64 pattern = 0;

	for (size_t iy = 0; iy < tileSize; ++iy)
	{
		size_t y = (startY + iy) % palletizedImage.m_height;

		for (size_t ix = 0; ix < tileSize; ++ix)
		{
			size_t x = (startX + ix) % palletizedImage.m_width;

			pattern = pattern << palletizedImage.m_bpp;
			pattern |= palletizedImage.m_pixels[y*palletizedImage.m_width + x];
		}
	}

	return (EPattern)pattern;
}

void GetPatterns (const SPalletizedImageData& palletizedImage, TPatternList& patterns, bool periodicInput, size_t tileSize)
{
	size_t maxX = palletizedImage.m_width - (periodicInput ? tileSize : 0);
	size_t maxY = palletizedImage.m_height - (periodicInput ? tileSize : 0);
	for (size_t y = 0; y < maxY; ++y)
	{
		for (size_t x = 0; x < maxX; ++x)
		{
			EPattern pattern = GetPattern(palletizedImage, x, y, tileSize);

            auto it = patterns.find(pattern);
            if (it != patterns.end())
                (*it).second++;
            else
                patterns.insert(std::make_pair(pattern, 1));
		}
	}
}

EPattern ReflectPatternXAxis (EPattern pattern, size_t tileSize, size_t bpp)
{
    uint64 ret = 0;
    for (size_t destY = 0; destY < tileSize; ++destY)
    {
        for (size_t destX = 0; destX < tileSize; ++destX)
        {
            size_t srcX = tileSize - 1 - destX;
            size_t srcY = destY;

            size_t srcPixelIndex = srcY * tileSize + srcX;

            uint64 srcMask = (1 << (bpp-1)) << srcPixelIndex;
            uint64 value = ((uint64)pattern & srcMask) >> srcPixelIndex;

            ret = ret << bpp;
            ret |= value;
        }
    }
    return (EPattern)ret;
}

EPattern RotatePatternCW90 (EPattern pattern, size_t tileSize, size_t bpp)
{
    uint64 ret = 0;
    for (size_t destY = 0; destY < tileSize; ++destY)
    {
        for (size_t destX = 0; destX < tileSize; ++destX)
        {
            size_t srcX = destY;
            size_t srcY = tileSize - 1 - destX;

            size_t srcPixelIndex = srcY * tileSize + srcX;

            uint64 srcMask = (1 << (bpp - 1)) << srcPixelIndex;
            uint64 value = ((uint64)pattern & srcMask) >> srcPixelIndex;

            ret = ret << bpp;
            ret |= value;
        }
    }
    return (EPattern)ret;
}

void RotateReflectPatterns (TPatternList& patterns, size_t symmetry, size_t tileSize, size_t bpp)
{
    TPatternList origPatterns = patterns;
    for (std::pair<EPattern,uint64> it : origPatterns)
    {
        std::array<EPattern, 8> rotatedReflectedPatterns;
        rotatedReflectedPatterns[0] = it.first;
        rotatedReflectedPatterns[1] = ReflectPatternXAxis(rotatedReflectedPatterns[0], tileSize, bpp);
        rotatedReflectedPatterns[2] = RotatePatternCW90(rotatedReflectedPatterns[0], tileSize, bpp);
        rotatedReflectedPatterns[3] = ReflectPatternXAxis(rotatedReflectedPatterns[2], tileSize, bpp);
        rotatedReflectedPatterns[4] = RotatePatternCW90(rotatedReflectedPatterns[2], tileSize, bpp);
        rotatedReflectedPatterns[5] = ReflectPatternXAxis(rotatedReflectedPatterns[4], tileSize, bpp);
        rotatedReflectedPatterns[6] = RotatePatternCW90(rotatedReflectedPatterns[4], tileSize, bpp);
        rotatedReflectedPatterns[7] = ReflectPatternXAxis(rotatedReflectedPatterns[6], tileSize, bpp);

        for (size_t i = 1; i < symmetry; ++i)
        {
            auto it = patterns.find(rotatedReflectedPatterns[i]);
            if (it != patterns.end())
                (*it).second++;
            else
                patterns.insert(std::make_pair(rotatedReflectedPatterns[i], 1));
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
    for (std::pair<EPattern, uint64> it : patterns)
    {
        for (size_t y = 0; y < tileSize; ++y)
        {
            for (size_t x = 0; x < tileSize; ++x)
            {
                size_t pixelIndex = y * tileSize + x;

                uint64 mask = (1 << (bpp - 1)) << pixelIndex;
                uint64 palleteIndex = ((uint64)it.first & mask) >> pixelIndex;

                *(SPixel*)&tempImageData.m_pixels[y * tempImageData.m_pitch + x * 3] = pallete[palleteIndex];
            }
        }

        char buffer[256];
        sprintf(buffer, ".Pattern%I64i.%I64i.bmp", it.first, it.second);

        char fileName[256];
        strcpy(fileName, srcFileName);
        strcat(fileName, buffer);

        SaveImage(fileName, tempImageData);
    }
}

void SaveFinalImage (const char* srcFileName, size_t width, size_t height, const TSuperpositionalPixels& superPositionalPixels)
{
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

    const SSuperpositionalPixel* srcPixel = &superPositionalPixels[0];
    for (size_t y = 0; y < height; ++y)
    {
        SPixel* destPixel = (SPixel*)&tempImageData.m_pixels[y*tempImageData.m_pitch];
        for (size_t x = 0; x < width; ++x, ++destPixel, ++srcPixel)
        {
            // TODO: decode pattern choices into a final image
            int ijkl = 0;
        }
    }

    char fileName[256];
    strcpy(fileName, srcFileName);
    strcat(fileName, ".out.bmp");

    SaveImage(fileName, tempImageData);
}

template <typename LAMBDA>
void ForEachPattern (const TPatternList& patterns, const LAMBDA& lambda)
{
    size_t patternIndex = 0;
    for (const std::pair<EPattern, uint64>& pair : patterns)
    {
        lambda(patternIndex, pair.first, pair.second);
        ++patternIndex;
    }

}

EGetPixelEntropyResult GetPixelEntropy (const std::vector<bool>& possiblePatterns, const TPatternList& patterns, float& entropy)
{
    // count how many patterns are possible for this pixel, and get a sum of the weights of those patterns
    uint64 possiblePatternCount = 0;
    uint64 possiblePatternWeight = 0;

    ForEachPattern(
        patterns,
        [&] (size_t patternIndex, EPattern pattern, uint64 patternWeight) {
            if (possiblePatterns[patternIndex])
            {
                ++possiblePatternCount;
                possiblePatternWeight += patternWeight;
            }
        }
    );

    // if there's no possibility of anything at all, this is an impossible pixel and we've failed.
    if (possiblePatternWeight == 0)
    {
        entropy = 0.0f;
        return EGetPixelEntropyResult::e_noPossibilities;
    }

    // if there's only one possibility, this pixel is decided, and there is no entropy.
    if (possiblePatternCount == 1)
    {
        entropy = 0.0f;
        return EGetPixelEntropyResult::e_OK;
    }

    // calculate entropy
    float logSum = std::log(float(possiblePatternWeight));
    float mainSum = 0.0f;
    ForEachPattern(
        patterns,
        [&] (size_t patternIndex, EPattern pattern, uint64 patternWeight) {
            if (possiblePatterns[patternIndex])
                mainSum += float(patternWeight) * (float)std::log(patternWeight);
        }
    );
    entropy = logSum - mainSum / possiblePatternWeight;
    return EGetPixelEntropyResult::e_OK;

    // TODO: should we add a small amount of noise? I'm leaning towards yes since the original did it, but dunno.
}

void ObservePixel (const TPatternList& patterns, SPRNG& prng, SSuperpositionalPixel& pixel)
{
    // choose a pattern randomly, taking weights into account.
    static std::vector<uint64> distribution;
    size_t numPatterns = patterns.size();
    distribution.resize(numPatterns);
    ForEachPattern(
        patterns,
        [&] (size_t patternIndex, EPattern pattern, uint64 patternWeight) {
            distribution[patternIndex] = patternWeight;
        }
    );
    std::discrete_distribution<uint64> dist(distribution.begin(), distribution.end());
    uint64 observedPatternIndex = prng.RandomDistribution(dist);

    // observe that pattern
    ForEachPattern(
        patterns,
        [&] (size_t patternIndex, EPattern pattern, uint64 patternWeight) {
            if (patternIndex != observedPatternIndex)
                pixel.m_possiblePatterns[patternIndex] = false;
        }
    );

    // remember that this pixel has been modified
    pixel.m_changed = true;
}

EObserveResult Observe (size_t width, size_t height, TSuperpositionalPixels& superpositionalPixels, const TPatternList& patterns, SPRNG& prng, size_t& undecidedPixels)
{
	// Find the pixel with the smallest entropy (uncertainty)
	size_t minEntropyPixelX = -1;
    size_t minEntropyPixelY = -1;
	float minEntropy = std::numeric_limits<float>::max();

    // TODO: move this to it's own function? FindMinimumEntropyPixel()?
    undecidedPixels = 0;
    SSuperpositionalPixel* pixel = &superpositionalPixels[0];
	for (size_t y = 0; y < height; ++y)
	{
		for (size_t x = 0; x < width; ++x, ++pixel)
		{
            float pixelEntropy = 0.0f;
            if (GetPixelEntropy(pixel->m_possiblePatterns, patterns, pixelEntropy) == EGetPixelEntropyResult::e_noPossibilities)
                return EObserveResult::e_failure;

            if (pixelEntropy == 0)
                continue;

            ++undecidedPixels;

            if (pixelEntropy < minEntropy)
            {
                minEntropy = pixelEntropy;
                minEntropyPixelX = x;
                minEntropyPixelY = y;
            }
		}
	}

    // if all pixels are decided (no entropy left in the image), return success
    if (minEntropyPixelX == -1 && minEntropyPixelY == -1)
        return EObserveResult::e_success;

    // otherwise, select a possibility for this pixel and return that we still have work to do
    ObservePixel(patterns, prng, superpositionalPixels[minEntropyPixelY*width+minEntropyPixelX]);
	return EObserveResult::e_notDone;
}

bool Propagate()
{
	// TODO: this
	return false;
}

void PropagateAllChanges()
{
	while (Propagate());
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
    GetPatterns(palletizedImage, patterns, periodicInput, c_tileSize);

    // Make rotations and reflections of those patterns (as controlled by symmetry parameter) to make our output image more diverse
    RotateReflectPatterns(patterns, symmetry, c_tileSize, palletizedImage.m_bpp);

    // Uncomment this to save the patterns it found
    //SavePatterns(patterns, c_fileName, c_tileSize, palletizedImage.m_bpp, palletizedImage.m_pallete);

	// Initialize stuff for waves
    TSuperpositionalPixels superpositionalPixels;
    superpositionalPixels.resize(outputImageWidth*outputImageHeight);
    for (SSuperpositionalPixel& pixel : superpositionalPixels)
    {
        pixel.m_changed = false;
        pixel.m_possiblePatterns.resize(patterns.size(), true);
    }

	// Do wave collapse
	EObserveResult observeResult = EObserveResult::e_notDone;
    uint32 lastPercent = 0;
    printf("Progress: 0%%");
	while (1)
	{
        size_t undecidedPixels;
		observeResult = Observe(outputImageWidth, outputImageHeight, superpositionalPixels, patterns, prng, undecidedPixels);
		if (observeResult != EObserveResult::e_notDone)
			break;

        uint32 percent = 100 - uint32(100.0f * float(undecidedPixels) / (float(outputImageWidth)*float(outputImageHeight)));
        
        if (lastPercent != percent)
        {
            printf("\rProgress: %i%%", percent);
            lastPercent = percent;
        }

		PropagateAllChanges();
	}

    // Save the final image
    SaveFinalImage(c_fileName, outputImageWidth, outputImageHeight, superpositionalPixels);
	return 0;
}

/*

TODO:

* print out timing and progress

? should we set a limit on how many iterations it can do?

* could make this oop - or should this stay as a single source file if possible?

! I really have no idea what the propagator array does.  It's 4d! [2*N-1][2*N-1][T][]

* make parameters come from command line?
 * could also just hard code it.  Maybe a macro list describing all the experiments?

* make params go into a special structure passed by const ref?

* support ground feature where there are specific pixels that are initialized and can't be changed.

* test! Maybe do all the tests from that XML file

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

* Notes:
 * The original code added noise to the entropy calculations to randomize it a bit.  The author said it made the animations more pleasing but wasn't sure if it made a difference to runtime.
 * could optimize, multithread, OOP.  Trying to focus on making a single cpp file that plainly describes things.

* Next:
 * simple tiled model
 * make some fast CPU version? multithreaded, focused on speed etc.
 * JFA?

*/