/*
	"Press When You Miss Me :3"
	Coded by ZcraftElite for my one true love, Ash :3
	Program to play music and randomly show a message to remind my boyfriend I love him
*/

// ---- Array Definition ----
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))


// ---- Included Libraries ----

// -- Audio Decoding --
#include <opusfile.h>   // .opus filetype Audio Decoding Library

// -- Main 3DS Library --
#include <3ds.h>        // 3DS Library

// -- Standard Libraries --
#include <stdio.h>      // Standard Input Output Library for C
#include <stdlib.h>     // Standard Library for C
#include <stdint.h>     // Integer Types Library for C
#include <string.h>     // String Manipulation Library for C
#include <time.h>       // Time Library for C
#include <unistd.h>     // Unix Standard Library for C


// ---- END Included Libraries ----



// ---- Audio Decoding Definitions ----

static const char *PATH = "romfs:/sample.opus";  // Path to Opus file to play

static const int SAMPLE_RATE = 48000;            // Opus is fixed at 48kHz
static const int SAMPLES_PER_BUF = SAMPLE_RATE * 120 / 1000;  // 120ms buffer
static const int CHANNELS_PER_SAMPLE = 2;        // We ask libopusfile for
                                                 // stereo output; it will down
                                                 // -mix for us as necessary.

static const int THREAD_AFFINITY = -1;           // Execute thread on any core
static const int THREAD_STACK_SZ = 32 * 1024;    // 32kB stack for audio thread

static const size_t WAVEBUF_SIZE = SAMPLES_PER_BUF * CHANNELS_PER_SAMPLE
    * sizeof(int16_t);                           // Size of NDSP wavebufs

// ---- END Audio Decoding Definitions ----

ndspWaveBuf s_waveBufs[3];
int16_t *s_audioBuffer = NULL;

LightEvent s_event;
volatile bool s_quit = false;  // Quit flag

// ---- HELPER FUNCTIONS ----

// Retrieve strings for libopusfile errors
// Sourced from David Gow's example code: https://davidgow.net/files/opusal.cpp
const char *opusStrError(int error)
{
    switch(error) {
        case OP_FALSE:
            return "OP_FALSE: A request did not succeed.";
        case OP_HOLE:
            return "OP_HOLE: There was a hole in the page sequence numbers.";
        case OP_EREAD:
            return "OP_EREAD: An underlying read, seek or tell operation "
                   "failed.";
        case OP_EFAULT:
            return "OP_EFAULT: A NULL pointer was passed where none was "
                   "expected, or an internal library error was encountered.";
        case OP_EIMPL:
            return "OP_EIMPL: The stream used a feature which is not "
                   "implemented.";
        case OP_EINVAL:
            return "OP_EINVAL: One or more parameters to a function were "
                   "invalid.";
        case OP_ENOTFORMAT:
            return "OP_ENOTFORMAT: This is not a valid Ogg Opus stream.";
        case OP_EBADHEADER:
            return "OP_EBADHEADER: A required header packet was not properly "
                   "formatted.";
        case OP_EVERSION:
            return "OP_EVERSION: The ID header contained an unrecognised "
                   "version number.";
        case OP_EBADPACKET:
            return "OP_EBADPACKET: An audio packet failed to decode properly.";
        case OP_EBADLINK:
            return "OP_EBADLINK: We failed to find data we had seen before or "
                   "the stream was sufficiently corrupt that seeking is "
                   "impossible.";
        case OP_ENOSEEK:
            return "OP_ENOSEEK: An operation that requires seeking was "
                   "requested on an unseekable stream.";
        case OP_EBADTIMESTAMP:
            return "OP_EBADTIMESTAMP: The first or last granule position of a "
                   "link failed basic validity checks.";
        default:
            return "Unknown error.";
    }
}

// Pause until user presses a button
void waitForInput(void) {
    printf("Press any button to exit...\n");
    while(aptMainLoop())
    {
        gspWaitForVBlank();
        gfxSwapBuffers();
        hidScanInput();

        if(hidKeysDown())
            break;
    }
}

// ---- END HELPER FUNCTIONS ----

// Audio initialisation code
// This sets up NDSP and our primary audio buffer
bool audioInit(void) {
    // Setup NDSP
    ndspChnReset(0);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    // Allocate audio buffer
    const size_t bufferSize = WAVEBUF_SIZE * ARRAY_SIZE(s_waveBufs);
    s_audioBuffer = (int16_t *)linearAlloc(bufferSize);
    if(!s_audioBuffer) {
        printf("Failed to allocate audio buffer\n");
        return false;
    }

    // Setup waveBufs for NDSP
    memset(&s_waveBufs, 0, sizeof(s_waveBufs));
    int16_t *buffer = s_audioBuffer;

    for(size_t i = 0; i < ARRAY_SIZE(s_waveBufs); ++i) {
        s_waveBufs[i].data_vaddr = buffer;
        s_waveBufs[i].status     = NDSP_WBUF_DONE;

        buffer += WAVEBUF_SIZE / sizeof(buffer[0]);
    }

    return true;
}

// Audio de-initialisation code
// Stops playback and frees the primary audio buffer
void audioExit(void) {
    ndspChnReset(0);
    linearFree(s_audioBuffer);
}

// Main audio decoding logic
// This function pulls and decodes audio samples from opusFile_ to fill waveBuf_
bool fillBuffer(OggOpusFile *opusFile_, ndspWaveBuf *waveBuf_) {
    #ifdef DEBUG
    // Setup timer for performance stats
    TickCounter timer;
    osTickCounterStart(&timer);
    #endif  // DEBUG

    // Decode samples until our waveBuf is full
    int totalSamples = 0;
    while(totalSamples < SAMPLES_PER_BUF) {
        int16_t *buffer = waveBuf_->data_pcm16 + (totalSamples *
            CHANNELS_PER_SAMPLE);
        const size_t bufferSize = (SAMPLES_PER_BUF - totalSamples) *
            CHANNELS_PER_SAMPLE;

        // Decode bufferSize samples from opusFile_ into buffer,
        // storing the number of samples that were decoded (or error)
        const int samples = op_read_stereo(opusFile_, buffer, bufferSize);
        if(samples <= 0) {
            if(samples == 0) break;  // No error here

            printf("op_read_stereo: error %d (%s)", samples,
                   opusStrError(samples));
            break;
        }
        
        totalSamples += samples;
    }

    // If no samples were read in the last decode cycle, we're done
    if(totalSamples == 0) {
        return false;
    }

    // Pass samples to NDSP
    waveBuf_->nsamples = totalSamples;
    ndspChnWaveBufAdd(0, waveBuf_);
    DSP_FlushDataCache(waveBuf_->data_pcm16,
        totalSamples * CHANNELS_PER_SAMPLE * sizeof(int16_t));

    #ifdef DEBUG
    // Print timing info
    osTickCounterUpdate(&timer);
    printf("fillBuffer %lfms in %lfms\n", totalSamples * 1000.0 / SAMPLE_RATE,
           osTickCounterRead(&timer));
    #endif  // DEBUG

    return true;
}

// NDSP audio frame callback
// This signals the audioThread to decode more things
// once NDSP has played a sound frame, meaning that there should be
// one or more available waveBufs to fill with more data.
void audioCallback(void *const nul_) {
    (void)nul_;  // Unused

    if(s_quit) { // Quit flag
        return;
    }
    
    LightEvent_Signal(&s_event);
}

// Audio thread
// This handles calling the decoder function to fill NDSP buffers as necessary
void audioThread(void *const opusFile_) {
    OggOpusFile *const opusFile = (OggOpusFile *)opusFile_;

    while(!s_quit) {  // Whilst the quit flag is unset,
                      // search our waveBufs and fill any that aren't currently
                      // queued for playback (i.e, those that are 'done')
        for(size_t i = 0; i < ARRAY_SIZE(s_waveBufs); ++i) {
            if(s_waveBufs[i].status != NDSP_WBUF_DONE) {
                continue;
            }
            
            if(!fillBuffer(opusFile, &s_waveBufs[i])) {   // Playback complete
                return;
            }
        }

        // Wait for a signal that we're needed again before continuing,
        // so that we can yield to other things that want to run
        // (Note that the 3DS uses cooperative threading)
        LightEvent_Wait(&s_event);
    }
}

// ---- MAIN FUNCTION ----

int main(int argc, char **argv)
{
	// Initialize services
	romfsInit();
    ndspInit();
	gfxInitDefault();
    
    // Sets the random number based on time
    srand(time(NULL));

	// In this example we need one PrintConsole for each screen
	PrintConsole topScreen, bottomScreen;

	// Initialize console for both screen using the two different PrintConsole we have defined
	consoleInit(GFX_TOP, &topScreen);
	consoleInit(GFX_BOTTOM, &bottomScreen);

	// Allows for N3DS 804MHz Operation, Works only on N3DS Models
	osSetSpeedupEnable(true);

	// Setup LightEvent for synchronisation of audioThread
    LightEvent_Init(&s_event, RESET_ONESHOT);

	// Open the Opus audio file
    int error = 0;
    OggOpusFile *opusFile = op_open_file(PATH, &error);
    if(error) {
        printf("Failed to open file: error %d (%s)\n", error,
               opusStrError(error));
        waitForInput();
    }

    // Attempt audioInit
    if(!audioInit()) {
        printf("Failed to initialise audio\n");
        waitForInput();

        gfxExit();
        ndspExit();
        romfsExit();
        return EXIT_FAILURE;
    }

    // Set the ndsp sound frame callback which signals our audioThread
    ndspSetCallback(audioCallback, NULL);

    // Spawn audio thread

    // Set the thread priority to the main thread's priority ...
    int32_t priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    // ... then subtract 1, as lower number => higher actual priority ...
    priority -= 1;
    // ... finally, clamp it between 0x18 and 0x3F to guarantee that it's valid.
    priority = priority < 0x18 ? 0x18 : priority;
    priority = priority > 0x3F ? 0x3F : priority;

    // Start the thread, passing our opusFile as an argument.
    const Thread threadId = threadCreate(audioThread, opusFile,
                                         THREAD_STACK_SZ, priority,
                                         THREAD_AFFINITY, false);

    // Code to calculate the amount of days me and Ash have been together
    // Set the target date (01/01/2023) [Placeholder date for privacy reasons]
    struct tm targetDate = {0};
    targetDate.tm_year = 2023 - 1900; // tm_year is years since 1900
    targetDate.tm_mon = 1 - 1; // tm_mon is 0-based (0 = January)
    targetDate.tm_mday = 1;

    // Get the current time
    time_t currentTime = time(NULL);
    struct tm *currentDate = gmtime(&currentTime);

    // Convert both dates to time_t for easier comparison
    time_t targetTime = mktime(&targetDate);
    double secondsDifference = difftime(currentTime, targetTime);
    int daysDifference = (int)(secondsDifference / (60 * 60 * 24)); // Convert seconds to days

    // Calculate approximate years and half-years
    int daysInYear = 365; // You could also use 365.25 if you want to consider leap years
    int daysInHalfYear = daysInYear / 2;

    // Vewy Cool Version Number Display and Loading Screen
    consoleSelect(&topScreen);

    printf("Press when you miss me :3\n");
    printf("By ZcraftElite\n");
    printf("v1.0.0\n\n");
    printf("Public Version: Display of the possibilities of this code\n");
    printf("Areas may be redacted for personal safety\n");

    sleep(2);
    printf("\x1b[30;10HPress the A button to countinue");

    while (aptMainLoop()) { // Waits to countinue until the user presses the A button
        hidScanInput();  // Scan all the inputs
        u32 kDown = hidKeysDown();  // Get the keys that were just pressed

        if (kDown & KEY_A) {
            break;  // Exit the loop if A is pressed
        }
    }

    consoleClear();

    // Also Vewy Cool Dedication Screen
    consoleSelect(&topScreen);

    printf("\x1b[14;12HDedicated to [------------]");
    printf("\x1b[15;19HMy Dino Nugget");

    sleep(2);
    printf("\x1b[30;10HPress the A button to countinue");

    while (aptMainLoop()) { // Waits to countinue until the user presses the A button
        hidScanInput();  // Scan all the inputs
        u32 kDown = hidKeysDown();  // Get the keys that were just pressed

        if (kDown & KEY_A) {
            break;  // Exit the loop if A is pressed
        }
    }

    consoleClear();

    // The Cutest Directions screen youll ever see :3
    consoleSelect(&topScreen);

    printf("DIRECTIONS:\n\n");
    printf("Dear, [-].\n\n");
    printf("It has been %d days since we first got together. ", daysDifference);
    
    if (daysDifference >= 2 * daysInYear) { // This code makes the program have more meaning, since overtime it will keep an up-to-date sentence on how long it has been :3
        printf("It's been exactly two years.\n");
    } else if (daysDifference >= daysInYear + daysInHalfYear) {
        printf("It's been almost two years.\n");
    } else if (daysDifference >= daysInYear) {
        printf("It's been over a year.\n");
    } else if (daysDifference >= daysInYear - 30) {
        printf("It's been almost a year.\n");
    } else {
        printf("That's quite a while!\n");
    }

    // This text IS a little hard to read when running on 3ds, no idea how to optimize that yet, look out for v1.0.1
    printf("I knew that you would have already cried just from me giving you a 3DS with all the games that you've missed since your old one broke. ");
    printf("But honestly, I hate making things easy for myself, and I knew that you were worth more than just a 3DS with some games. ");
    printf("I was thinking about how much you say you miss me when we cant call or when im busy working on a project or something. ");
    printf("So I made this, a app that you can open whenever you want on your DS and be reminded how much your wifey loves you. :3 ");
    printf("Every time you open it, it will give you a random paragraph of love written by me to you, the love of my life. ");
    printf("I don't suggest hitting the A button until your ready to be bombarded with love. ");
    printf("And yes, undertale music :P");

    sleep(2);
    printf("\x1b[30;10HPress the A button to countinue");

    while (aptMainLoop()) { // Waits to countinue until the user presses the A button
        hidScanInput();  // Scan all the inputs
        u32 kDown = hidKeysDown();  // Get the keys that were just pressed

        if (kDown & KEY_A) {
            break;  // Exit the loop if A is pressed
        }
    }

    consoleClear();

generatemsg:
    // Before we can start printing my cute little messages on the screen we have to determine what the random number is
     int randomNumber = rand() % 11;

	// Before doing any text printing we should select the PrintConsole in which we are willing to write, otherwise the library will print on the last selected/initialized one
	// Let's start by printing something on the top screen
	consoleSelect(&topScreen);
    switch(randomNumber) {
        case 0:
            printf("You know, sometimes I dream about what it would be like to be adults togther, to become a family of our own.\n");
            printf("I can imagine just waking up every morning with you cuddling me.\n");
            printf("One morning I woke up crying...\n");
            printf("Because I had a dream about you asking me to marry you-\n");
            printf("To this day, that was my favorite dream ever...\n\n");
            printf("I wuv yous <3 (01/11)");
            break;
        case 1:
            printf("Nobody could ever convince me otherwise...\n");
            printf("Your nose is to most boopable nose in the world.\n");
            printf("I could just... *boop*\n");
            printf("And then watch the red spread accross your face...\n");
            printf("As you blush wildly!\n\n");
            printf("I wuv yous <3 (02/11)");
            break;
        case 2:
            printf("You probably already know this...\n");
            printf("But I could literally spend centuries staring at ur face :3\n");
            printf("To this day, I still don't know what it is about it...\n");
            printf("Yous just so cute >w<\n\n");
            printf("I wuv yous <3 (03/11)");
            break;
        case 3:
            printf("I wish I could spend entire days...\n");
            printf("Just us two, running around in circles like dogs.\n");
            printf("Both playfighting with eachother until we sweat and pass out.\n");
            printf("Its a big dream of mine >w<\n\n");
            printf("I wuv yous <3 (04/11)");
            break;
        case 4:
            printf("Sometimes, we get into arguments :(\n");
            printf("I always have faith that we will solve them, IDEK why.\n");
            printf("Maybe it's because of how much I know we care about eachother.\n");
            printf("God, you make me so happy...\n\n");
            printf("I wuv yous <3 (05/11)");
            break;
        case 5:
            printf("*boop*\n");
            printf("Get booped bitch /J >:3\n\n");
            printf("I wuv yous <3 (06/11)");
            break;
        case 6:
            printf("I feel like giving a hug.\n");
            printf("*hugs tightly*\n\n");
            printf("I wuv yous <3 (07/11)");
            break;
        case 7:
            printf("If you can see this...\n");
            printf("Then your gonna get tackled with love next time I see you >:3\n\n");
            printf("I wuv yous <3 (08/11)");
            break;
        case 8:
            printf("You remember those nights...\n");
            printf("When we would talk about our future house?\n");
            printf("Every time I knew...\n");
            printf("I would do anything to make it happen for reals >;3\n\n");
            printf("I wuv yous <3 (09/11)");
            break;
        case 9:
            printf("You got message number 10 :O\n");
            printf("This one is just talking about how much I wuv yous.\n");
            printf("AND HOW MUCH OF A GOD DAMN CUTIE YOU ARE!!!\n");
            printf("Your totally gonna get made out with next time I see you.\n\n");
            printf("I wuv yous <3 (10/11)");
            break;
        case 10:
            printf("Guess what...\n");
            printf("YOUS IS CUTE >:3\n");
            printf("YOUS IS CUTE! YOUS IS CUTE! YOUS IS CUTE!\n");
            printf("UwU\n\n");
            printf("I wuv yous <3 (11/11)");
            break;
        default:
            printf("Unexpected number!\n");
            break;
    }
	

	// My signature :3
	consoleSelect(&topScreen);
	printf("\x1b[28;17H\x1b[35mFrom ur wifey poo\x1b[0m");
    printf("\x1b[29;22H\x1b[35mZena :3\x1b[0m");

    // New message hint
    sleep(2);
	consoleSelect(&bottomScreen);
	printf("\x1b[28;4HNEW MSG");
    printf("\x1b[30;7HV");

    // Exit hint
    sleep(2);
	consoleSelect(&bottomScreen);
	printf("\x1b[28;30HEXIT APP");
    printf("\x1b[30;33HV");

	// Main loop
	while (aptMainLoop())
	{
		//Scan all the inputs. This should be done once for each frame
		hidScanInput();

		//hidKeysDown returns information about which buttons have been just pressed (and they weren't in the previous frame)
		u32 kDown = hidKeysDown();\

        if (kDown & KEY_SELECT) {
            consoleSelect(&topScreen);
            consoleClear();
            consoleSelect(&bottomScreen);
            consoleClear();
            goto generatemsg;
        }

		if (kDown & KEY_START) break; // break in order to return to hbmenu

		// Flush and swap framebuffers
		gfxFlushBuffers();
		gfxSwapBuffers();

		//Wait for VBlank
		gspWaitForVBlank();
	}

	// Signal audio thread to quit
    s_quit = true;
    LightEvent_Signal(&s_event);

    // Free the audio thread
    threadJoin(threadId, UINT64_MAX);
    threadFree(threadId);

    // Cleanup audio things and de-init platform features
    audioExit();
    ndspExit();
    op_free(opusFile);

	// Exit Services
    romfsExit();
    gfxExit();

	return 0;
}
