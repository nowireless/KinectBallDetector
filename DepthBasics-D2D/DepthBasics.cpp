//------------------------------------------------------------------------------
// <copyright file="DepthBasics.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#include "stdafx.h"
#include <strsafe.h>
#include <cmath>
#include "DepthBasics.h"
#include "resource.h"


#define MIN_DEPTH                   400
#define MAX_DEPTH                   16383
#define UNKNOWN_DEPTH               0
#define UNKNOWN_DEPTH_COLOR         0x003F3F07
#define TOO_NEAR_COLOR              0x001F7FFF
#define TOO_FAR_COLOR               0x007F0F3F
#define NEAREST_COLOR               0x00FFFFFF

#define COLOR_INDEX_BLUE            0
#define COLOR_INDEX_GREEN           1
#define COLOR_INDEX_RED             2
#define COLOR_INDEX_ALPHA           3
#define BYTES_PER_PIXEL_RGB         4

#define BYTES_PER_PIXEL_DEPTH sizeof(NUI_DEPTH_IMAGE_PIXEL)

const BYTE CDepthBasics::m_intensityShiftR[] = { 0, 2, 0, 2, 0, 0, 2 };
const BYTE CDepthBasics::m_intensityShiftG[] = { 0, 2, 2, 0, 2, 0, 0 };
const BYTE CDepthBasics::m_intensityShiftB[] = { 0, 0, 2, 2, 0, 2, 0 };

/// <summary>
/// Entry point for the application
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="hPrevInstance">always 0</param>
/// <param name="lpCmdLine">command line arguments</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
/// <returns>status</returns>
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    CDepthBasics application;
    application.Run(hInstance, nCmdShow);
}

/// <summary>
/// Constructor
/// </summary>
CDepthBasics::CDepthBasics() :
    m_pD2DFactory(NULL),
    m_pDrawDepth(NULL),
    m_hNextDepthFrameEvent(INVALID_HANDLE_VALUE),
    m_pDepthStreamHandle(INVALID_HANDLE_VALUE),
    m_bNearMode(false),
    m_pNuiSensor(NULL),
	m_depthTreatment(CLAMP_UNRELIABLE_DEPTHS),
	m_nearMode(false)
{
    // create heap storage for depth pixel data in RGBX format
	InitDepthColorTable();
    m_depthRGBX = new BYTE[cDepthWidth*cDepthHeight*cBytesPerPixel];
}

/// <summary>
/// Destructor
/// </summary>
CDepthBasics::~CDepthBasics()
{
    if (m_pNuiSensor)
    {
        m_pNuiSensor->NuiShutdown();
    }

    if (m_hNextDepthFrameEvent != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hNextDepthFrameEvent);
    }

    // clean up Direct2D renderer
    delete m_pDrawDepth;
    m_pDrawDepth = NULL;

    // done with depth pixel data
    delete[] m_depthRGBX;

    // clean up Direct2D
    SafeRelease(m_pD2DFactory);

    SafeRelease(m_pNuiSensor);
}

/// <summary>
/// Creates the main window and begins processing
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
int CDepthBasics::Run(HINSTANCE hInstance, int nCmdShow)
{
    MSG       msg = {0};
    WNDCLASS  wc;

    // Dialog custom window class
    ZeroMemory(&wc, sizeof(wc));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.cbWndExtra    = DLGWINDOWEXTRA;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
    wc.lpfnWndProc   = DefDlgProcW;
    wc.lpszClassName = L"DepthBasicsAppDlgWndClass";

    if (!RegisterClassW(&wc))
    {
        return 0;
    }

    // Create main application window
    HWND hWndApp = CreateDialogParamW(
        hInstance,
        MAKEINTRESOURCE(IDD_APP),
        NULL,
        (DLGPROC)CDepthBasics::MessageRouter, 
        reinterpret_cast<LPARAM>(this));

    // Show window
    ShowWindow(hWndApp, nCmdShow);

    const int eventCount = 1;
    HANDLE hEvents[eventCount];

    // Main message loop
    while (WM_QUIT != msg.message)
    {
        hEvents[0] = m_hNextDepthFrameEvent;

        // Check to see if we have either a message (by passing in QS_ALLINPUT)
        // Or a Kinect event (hEvents)
        // Update() will check for Kinect events individually, in case more than one are signalled
        MsgWaitForMultipleObjects(eventCount, hEvents, FALSE, INFINITE, QS_ALLINPUT);

        // Explicitly check the Kinect frame event since MsgWaitForMultipleObjects
        // can return for other reasons even though it is signaled.
        Update();

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            // If a dialog message will be taken care of by the dialog proc
            if ((hWndApp != NULL) && IsDialogMessageW(hWndApp, &msg))
            {
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}

/// <summary>
/// Main processing function
/// </summary>
void CDepthBasics::Update()
{
    if (NULL == m_pNuiSensor)
    {
        return;
    }

    if ( WAIT_OBJECT_0 == WaitForSingleObject(m_hNextDepthFrameEvent, 0) )
    {
        ProcessDepth();
    }
}

/// <summary>
/// Handles window messages, passes most to the class instance to handle
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CDepthBasics::MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CDepthBasics* pThis = NULL;
    
    if (WM_INITDIALOG == uMsg)
    {
        pThis = reinterpret_cast<CDepthBasics*>(lParam);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<CDepthBasics*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pThis)
    {
        return pThis->DlgProc(hWnd, uMsg, wParam, lParam);
    }

    return 0;
}

/// <summary>
/// Handle windows messages for the class instance
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CDepthBasics::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_INITDIALOG:
        {
            // Bind application window handle
            m_hWnd = hWnd;

            // Init Direct2D
            D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);

            // Create and initialize a new Direct2D image renderer (take a look at ImageRenderer.h)
            // We'll use this to draw the data we receive from the Kinect to the screen
            m_pDrawDepth = new ImageRenderer();
            HRESULT hr = m_pDrawDepth->Initialize(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), m_pD2DFactory, cDepthWidth, cDepthHeight, cDepthWidth * sizeof(long));
            if (FAILED(hr))
            {
                SetStatusMessage(L"Failed to initialize the Direct2D draw device.");
            }

            // Look for a connected Kinect, and create it if found
            CreateFirstConnected();
        }
        break;

        // If the titlebar X is clicked, destroy app
        case WM_CLOSE:
            DestroyWindow(hWnd);
            break;

        case WM_DESTROY:
            // Quit the main message pump
            PostQuitMessage(0);
            break;

        // Handle button press
        case WM_COMMAND:
            // If it was for the near mode control and a clicked event, change near mode
            if (IDC_CHECK_NEARMODE == LOWORD(wParam) && BN_CLICKED == HIWORD(wParam))
            {
                // Toggle out internal state for near mode
                m_bNearMode = !m_bNearMode;

                if (NULL != m_pNuiSensor)
                {
                    // Set near mode based on our internal state
                    m_pNuiSensor->NuiImageStreamSetImageFrameFlags(m_pDepthStreamHandle, m_bNearMode ? NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE : 0);
                }
            }
            break;
    }

    return FALSE;
}

/// <summary>
/// Create the first connected Kinect found 
/// </summary>
/// <returns>indicates success or failure</returns>
HRESULT CDepthBasics::CreateFirstConnected()
{
    INuiSensor * pNuiSensor;
    HRESULT hr;

    int iSensorCount = 0;
    hr = NuiGetSensorCount(&iSensorCount);
    if (FAILED(hr))
    {
        return hr;
    }

    // Look at each Kinect sensor
    for (int i = 0; i < iSensorCount; ++i)
    {
        // Create the sensor so we can check status, if we can't create it, move on to the next
        hr = NuiCreateSensorByIndex(i, &pNuiSensor);
        if (FAILED(hr))
        {
            continue;
        }

        // Get the status of the sensor, and if connected, then we can initialize it
        hr = pNuiSensor->NuiStatus();
        if (S_OK == hr)
        {
            m_pNuiSensor = pNuiSensor;
            break;
        }

        // This sensor wasn't OK, so release it since we're not using it
        pNuiSensor->Release();
    }

    if (NULL != m_pNuiSensor)
    {
        // Initialize the Kinect and specify that we'll be using depth
        hr = m_pNuiSensor->NuiInitialize(NUI_INITIALIZE_FLAG_USES_DEPTH); 
        if (SUCCEEDED(hr))
        {
            // Create an event that will be signaled when depth data is available
            m_hNextDepthFrameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

            // Open a depth image stream to receive depth frames
            hr = m_pNuiSensor->NuiImageStreamOpen(
                NUI_IMAGE_TYPE_DEPTH,
                NUI_IMAGE_RESOLUTION_640x480,
                0,
                2,
                m_hNextDepthFrameEvent,
                &m_pDepthStreamHandle);
        }
    }

    if (NULL == m_pNuiSensor || FAILED(hr))
    {
        SetStatusMessage(L"No ready Kinect found!");
        return E_FAIL;
    }

    return hr;
}

/// <summary>
/// Handle new depth data
/// </summary>
void CDepthBasics::ProcessDepth()
{
    HRESULT hr;
    NUI_IMAGE_FRAME imageFrame;

    // Attempt to get the depth frame
    hr = m_pNuiSensor->NuiImageStreamGetNextFrame(m_pDepthStreamHandle, 0, &imageFrame);
    if (FAILED(hr))
    {
        return;
    }

    BOOL nearMode;
    INuiFrameTexture* pTexture;

    // Get the depth image pixel texture
    hr = m_pNuiSensor->NuiImageFrameGetDepthImagePixelFrameTexture(
        m_pDepthStreamHandle, &imageFrame, &nearMode, &pTexture);
    if (FAILED(hr))
    {
        goto ReleaseFrame;
    }

    NUI_LOCKED_RECT LockedRect;

    // Lock the frame data so the Kinect knows not to modify it while we're reading it
    pTexture->LockRect(0, &LockedRect, NULL, 0);

    // Make sure we've received valid data
    if (LockedRect.Pitch != 0)
    {
        // Get the min and max reliable depth for the current frame
        int minDepth = (nearMode ? NUI_IMAGE_DEPTH_MINIMUM_NEAR_MODE : NUI_IMAGE_DEPTH_MINIMUM) >> NUI_IMAGE_PLAYER_INDEX_SHIFT;
        int maxDepth = (nearMode ? NUI_IMAGE_DEPTH_MAXIMUM_NEAR_MODE : NUI_IMAGE_DEPTH_MAXIMUM) >> NUI_IMAGE_PLAYER_INDEX_SHIFT;

        BYTE * rgbrun = m_depthRGBX;
		
		const NUI_DEPTH_IMAGE_PIXEL * pBufferRun = reinterpret_cast<const NUI_DEPTH_IMAGE_PIXEL *>(LockedRect.pBits);

        // end pixel is start + width*height - 1
        const NUI_DEPTH_IMAGE_PIXEL * pBufferEnd = pBufferRun + (cDepthWidth * cDepthHeight);

		USHORT minReliableDepth = (m_nearMode ? NUI_IMAGE_DEPTH_MINIMUM_NEAR_MODE : NUI_IMAGE_DEPTH_MINIMUM) >> NUI_IMAGE_PLAYER_INDEX_SHIFT;
		USHORT maxReliableDepth = (m_nearMode ? NUI_IMAGE_DEPTH_MAXIMUM_NEAR_MODE : NUI_IMAGE_DEPTH_MAXIMUM) >> NUI_IMAGE_PLAYER_INDEX_SHIFT;

        while ( pBufferRun < pBufferEnd )
        {
            // discard the portion of the depth that contains only the player index
            USHORT depth = pBufferRun->depth;
			USHORT index = pBufferRun->playerIndex;

			//USHORT depth = NuiDepthPixelToDepth(pBufferRun->depth);
			//USHORT index = NuiDepthPixelToPlayerIndex(pBufferRun->depth);
            // To convert to a byte, we're discarding the most-significant
            // rather than least-significant bits.
            // We're preserving detail, although the intensity will "wrap."
            // Values outside the reliable depth range are mapped to 0 (black).

            // Note: Using conditionals in this loop could degrade performance.
            // Consider using a lookup table instead when writing production code.
            //BYTE intensity = static_cast<BYTE>(depth >= minDepth && depth <= maxDepth ? depth % 256 : 0);
			BYTE r;
			BYTE g;
			BYTE b;
			if (index == 0 && depth == 0) {
				//Unknown Depth
				r = 63;
				g = 63;
				b = 7;
			} else  if (index == 0 && depth < minReliableDepth) {
				//Too Near
				r = 31;
				g = 127;
				b = 255;
			} else if (index == 0 && depth > maxReliableDepth && depth <= USHRT_MAX) {
				//Too Far
				r = 127;
				g = 15;
				b = 63;
			} else {
				BYTE intensity = GetIntensity(depth);
				r = intensity >> m_intensityShiftR[index];
				g = intensity >> m_intensityShiftG[index];
				b = intensity >> m_intensityShiftB[index];
			}
			//BYTE *
            // Write out blue byte
            //*(rgbrun++) = intensity;
			*(rgbrun++) = b;
            
			// Write out green byte
            //*(rgbrun++) = intensity;
			*(rgbrun++) = g;
          
			// Write out red byte
            //*(rgbrun++) = intensity;
			*(rgbrun++) = r;
			
			//*rgbrun = m_depthColorTable[index][depth];
			//rgbrun++;
			//rgbrun++;
			//rgbrun++;
			

            // We're outputting BGR, the last byte in the 32 bits is unused so skip it
            // If we were outputting BGRA, we would write alpha here.
            //*(rgbrun++) = 1;
			++rgbrun;

            // Increment our index into the Kinect's depth buffer
            ++pBufferRun;
        }

        // Draw the data with Direct2D
        m_pDrawDepth->Draw(m_depthRGBX, cDepthWidth * cDepthHeight * cBytesPerPixel);
    }

    // We're done with the texture so unlock it
    pTexture->UnlockRect(0);

    pTexture->Release();

ReleaseFrame:
    // Release the frame
    m_pNuiSensor->NuiImageStreamReleaseFrame(m_pDepthStreamHandle, &imageFrame);
}

/// <summary>
/// Set the status bar message
/// </summary>
/// <param name="szMessage">message to display</param>
void CDepthBasics::SetStatusMessage(WCHAR * szMessage)
{
    SendDlgItemMessageW(m_hWnd, IDC_STATUS, WM_SETTEXT, 0, (LPARAM)szMessage);
}



/// <summary>
/// Initialize the depth-color mapping table.
/// </summary>
void CDepthBasics::InitDepthColorTable()
{
	// Get the min and max reliable depth
	USHORT minReliableDepth = (m_nearMode ? NUI_IMAGE_DEPTH_MINIMUM_NEAR_MODE : NUI_IMAGE_DEPTH_MINIMUM) >> NUI_IMAGE_PLAYER_INDEX_SHIFT;
	USHORT maxReliableDepth = (m_nearMode ? NUI_IMAGE_DEPTH_MAXIMUM_NEAR_MODE : NUI_IMAGE_DEPTH_MAXIMUM) >> NUI_IMAGE_PLAYER_INDEX_SHIFT;

	ZeroMemory(m_depthColorTable, sizeof(m_depthColorTable));

	// Set color for unknown depth
	m_depthColorTable[0][UNKNOWN_DEPTH] = UNKNOWN_DEPTH_COLOR;

	switch (m_depthTreatment)
	{
	case CLAMP_UNRELIABLE_DEPTHS:
		// Fill in the "near" portion of the table with solid color
		for (int depth = UNKNOWN_DEPTH + 1; depth < minReliableDepth; depth++)
		{
			m_depthColorTable[0][depth] = TOO_NEAR_COLOR;
		}

		// Fill in the "far" portion of the table with solid color
		for (int depth = maxReliableDepth + 1; depth <= USHRT_MAX; depth++)
		{
			m_depthColorTable[0][depth] = TOO_FAR_COLOR;
		}
		break;

	case TINT_UNRELIABLE_DEPTHS:
	{
								   // Fill in the "near" portion of the table with a tinted gradient
								   for (int depth = UNKNOWN_DEPTH + 1; depth < minReliableDepth; depth++)
								   {
									   BYTE intensity = GetIntensity(depth);
									   BYTE r = intensity >> 3;
									   BYTE g = intensity >> 1;
									   BYTE b = intensity;
									   SetColor(&m_depthColorTable[0][depth], r, g, b);
								   }

								   // Fill in the "far" portion of the table with a tinted gradient
								   for (int depth = maxReliableDepth + 1; depth <= USHRT_MAX; depth++)
								   {
									   BYTE intensity = GetIntensity(depth);
									   BYTE r = intensity;
									   BYTE g = intensity >> 3;
									   BYTE b = intensity >> 1;
									   SetColor(&m_depthColorTable[0][depth], r, g, b);
								   }
	}
		break;

	case DISPLAY_ALL_DEPTHS:
		minReliableDepth = MIN_DEPTH;
		maxReliableDepth = MAX_DEPTH;

		for (int depth = UNKNOWN_DEPTH + 1; depth < minReliableDepth; depth++)
		{
			m_depthColorTable[0][depth] = NEAREST_COLOR;
		}
		break;

	default:
		break;
	}

	for (USHORT depth = minReliableDepth; depth <= maxReliableDepth; depth++)
	{
		BYTE intensity = GetIntensity(depth);

		for (int index = 0; index <= MAX_PLAYER_INDEX; index++)
		{
			BYTE r = intensity >> m_intensityShiftR[index];
			BYTE g = intensity >> m_intensityShiftG[index];
			BYTE b = intensity >> m_intensityShiftB[index];
			SetColor(&m_depthColorTable[index][depth], r, g, b);
		}
	}
}

/// <summary>
/// Calculate intensity of a certain depth
/// </summary>
/// <param name="depth">A certain depth</param>
/// <returns>Intensity calculated from a certain depth</returns>
BYTE CDepthBasics::GetIntensity(int depth)
{
	// Validate arguments
	if (depth < MIN_DEPTH || depth > MAX_DEPTH)
	{
		return UCHAR_MAX;
	}

	// Use a logarithmic scale that shows more detail for nearer depths.
	// The constants in this formula were chosen such that values between
	// MIN_DEPTH and MAX_DEPTH will map to the full range of possible
	// byte values.
	const float depthRangeScale = 500.0f;
	const int intensityRangeScale = 74;
	return (BYTE)(~(BYTE)min(
		UCHAR_MAX,
		log((double)(depth - MIN_DEPTH) / depthRangeScale + 1) * intensityRangeScale));
}


void CDepthBasics::SetColor(UINT* pColor, BYTE red, BYTE green, BYTE blue, BYTE alpha)
{
	if (!pColor)
		return;

	BYTE* c = (BYTE*)pColor;
	c[COLOR_INDEX_RED] = red;
	c[COLOR_INDEX_GREEN] = green;
	c[COLOR_INDEX_BLUE] = blue;
	c[COLOR_INDEX_ALPHA] = alpha;
}