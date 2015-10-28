// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "MovieSceneCapturePCH.h"
#include "MovieSceneCapture.h"

#if WITH_EDITOR
#include "ImageWrapper.h"
#endif

#include "MovieSceneCaptureModule.h"

struct FUniqueMovieSceneCaptureHandle : FMovieSceneCaptureHandle
{
	FUniqueMovieSceneCaptureHandle()
	{
		/// Start IDs at index 1 since 0 is deemed invalid
		static uint32 Unique = 1;
		ID = Unique++;
	}
};

FMovieSceneCaptureSettings::FMovieSceneCaptureSettings()
	: Resolution(1280, 720)
{
	OutputDirectory.Path = FPaths::VideoCaptureDir();
	OutputFormat = NSLOCTEXT("MovieCapture", "DefaultFormat", "MovieCapture_{width}x{height}_{quality}").ToString();
	FrameRate = 24;
	CaptureType = EMovieCaptureType::AVI;
	bUseCompression = true;
	CompressionQuality = 1.f;
	bEnableTextureStreaming = true;
	bCinematicMode = true;
	bAllowMovement = true;
	bAllowTurning = true;
	bShowPlayer = true;
	bShowHUD = true;
}

UMovieSceneCapture::UMovieSceneCapture(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
	bBufferVisualizationDumpFrames = false;

	TArray<FString> Tokens, Switches;
	FCommandLine::Parse( FCommandLine::Get(), Tokens, Switches );
	for (auto& Switch : Switches)
	{
		AdditionalCommandLineArguments.AppendChar('-');
		AdditionalCommandLineArguments.Append(Switch);
		AdditionalCommandLineArguments.AppendChar(' ');
	}
	// the PIEVIACONSOLE parameter tells UGameEngine to add the auto-save dir to the paths array and repopulate the package file cache
	// this is needed in order to support streaming levels as the streaming level packages will be loaded only when needed (thus
	// their package names need to be findable by the package file caching system)
	// (we add to EditorCommandLine because the URL is ignored by WindowsTools)
	AdditionalCommandLineArguments += TEXT("-PIEVIACONSOLE -nomovie ");

	// renderer overrides - hack
	AdditionalCommandLineArguments += FParse::Param(FCommandLine::Get(), TEXT("d3d11"))		?	TEXT("-d3d11 ")		: TEXT("");
	AdditionalCommandLineArguments += FParse::Param(FCommandLine::Get(), TEXT("sm5"))		?	TEXT("-sm5 ")		: TEXT("");
	AdditionalCommandLineArguments += FParse::Param(FCommandLine::Get(), TEXT("dx11"))		?	TEXT("-dx11 ")		: TEXT("");
	AdditionalCommandLineArguments += FParse::Param(FCommandLine::Get(), TEXT("d3d10"))		?	TEXT("-d3d10 ")		: TEXT("");
	AdditionalCommandLineArguments += FParse::Param(FCommandLine::Get(), TEXT("sm4"))		?	TEXT("-sm4 ")		: TEXT("");
	AdditionalCommandLineArguments += FParse::Param(FCommandLine::Get(), TEXT("dx10"))		?	TEXT("-dx10 ")		: TEXT("");
	AdditionalCommandLineArguments += FParse::Param(FCommandLine::Get(), TEXT("opengl"))	?	TEXT("-opengl ")	: TEXT("");
	AdditionalCommandLineArguments += FParse::Param(FCommandLine::Get(), TEXT("opengl3"))	?	TEXT("-opengl3 ")	: TEXT("");
	AdditionalCommandLineArguments += FParse::Param(FCommandLine::Get(), TEXT("opengl4"))	?	TEXT("-opengl4 ")	: TEXT("");

	Handle = FUniqueMovieSceneCaptureHandle();
}

void UMovieSceneCapture::Initialize(TWeakPtr<FSceneViewport> InSceneViewport)
{
	SceneViewport = InSceneViewport;
}

void UMovieSceneCapture::PrepareForScreenshot()
{
	auto Viewport = SceneViewport.Pin();
	if (!Viewport.IsValid())
	{
		return;
	}

	auto ViewportWidget = Viewport->GetViewportWidget().Pin();
	if (!ViewportWidget.IsValid())
	{
		return;
	}

	auto& Application = FSlateApplication::Get();

	// We can't screenshot the widget unless there's a valid window handle to draw it in.
	TSharedPtr<SWindow> WidgetWindow = Application.FindWidgetWindow(ViewportWidget.ToSharedRef());
	if (!WidgetWindow.IsValid())
	{
		return;
	}

	FWidgetPath WidgetPath;
	FSlateApplication::Get().GeneratePathToWidgetChecked(ViewportWidget.ToSharedRef(), WidgetPath);

	FArrangedWidget ArrangedWidget = WidgetPath.FindArrangedWidget(ViewportWidget.ToSharedRef()).Get(FArrangedWidget::NullWidget);

	FVector2D Position = ArrangedWidget.Geometry.AbsolutePosition;
	FVector2D WindowPosition = WidgetWindow->GetPositionInScreen();

	const int32 RelativePositionX = Position.X - WindowPosition.X;
	const int32 RelativePositionY = Position.Y - WindowPosition.Y;

	FVector2D Size = ArrangedWidget.Geometry.GetDrawSize();

	FIntRect ScreenshotRect(
		RelativePositionX,
		RelativePositionY,
		RelativePositionX + Size.X,
		RelativePositionY + Size.Y);

	Application.GetRenderer()->PrepareToTakeScreenshot(ScreenshotRect, &ScratchBuffer);
}

void UMovieSceneCapture::StartCapture()
{
	auto Viewport = SceneViewport.Pin();
	if (!Viewport.IsValid())
	{
		return;
	}

	if (!CaptureStrategy.IsValid())
	{
		CaptureStrategy = MakeShareable(new FRealTimeCaptureStrategy(Settings.FrameRate));
	}

	CaptureStrategy->OnStart();

	CachedMetrics.ElapsedSeconds = 0;

	if (bBufferVisualizationDumpFrames)
	{
		static IConsoleVariable* CVarDumpFrames = IConsoleManager::Get().FindConsoleVariable(TEXT("r.BufferVisualizationDumpFrames"));
		if (CVarDumpFrames)
		{
			CVarDumpFrames->Set(1, ECVF_SetByCommandline);
		}
	}

	CachedMetrics.Width = Viewport->GetSize().X;
	CachedMetrics.Height = Viewport->GetSize().Y;

	PrepareForScreenshot();

	if (Settings.CaptureType == EMovieCaptureType::AVI)
	{
		FAVIWriterOptions Options;
		Options.OutputFilename = ResolveUniqueFilename();
		Options.CaptureFPS = Settings.FrameRate;
		Options.CodecName = Settings.Codec;
		Options.bSynchronizeFrames = CaptureStrategy->ShouldSynchronizeFrames();

		if (Settings.bUseCompression)
		{
			Options.CompressionQuality = Settings.CompressionQuality;
		}

		AVIWriter.Reset(FAVIWriter::CreateInstance(Options));
		AVIWriter->StartCapture(Viewport);
	}
}

void UMovieSceneCapture::CaptureFrame(float DeltaSeconds)
{
	// Tell slate to capture the *next* frame
	PrepareForScreenshot();

	auto Viewport = SceneViewport.Pin();
	if (!CaptureStrategy.IsValid() || !Viewport.IsValid() || !ScratchBuffer.Num())
	{
		return;
	}

	CachedMetrics.ElapsedSeconds += DeltaSeconds;

	// By this point, the slate application has already populated our scratch buffer
	if (CaptureStrategy->ShouldPresent(CachedMetrics.ElapsedSeconds, CachedMetrics.Frame))
	{
		TArray<FColor> ThisFrameBuffer;
		Swap(ThisFrameBuffer, ScratchBuffer);

		uint32 NumDroppedFrames = CaptureStrategy->GetDroppedFrames(CachedMetrics.ElapsedSeconds, CachedMetrics.Frame);
		CachedMetrics.Frame += NumDroppedFrames;

		CaptureStrategy->OnPresent(CachedMetrics.ElapsedSeconds, CachedMetrics.Frame);
		++CachedMetrics.Frame;
		
		if (AVIWriter)
		{
			AVIWriter->DropFrames(NumDroppedFrames);
			AVIWriter->Update(CachedMetrics.ElapsedSeconds, MoveTemp(ThisFrameBuffer));
		}
#if WITH_EDITOR
		else
		{
			SaveFrameToFile(MoveTemp(ThisFrameBuffer));
		}
#endif
	}
}

void UMovieSceneCapture::StopCapture()
{
	CaptureStrategy->OnStop();
	CaptureStrategy = nullptr;

	if (AVIWriter)
	{
		AVIWriter->StopCapture();
		AVIWriter.Reset();
	}
}

void UMovieSceneCapture::Close()
{
	StopCapture();
	IMovieSceneCaptureModule::Get().OnMovieSceneCaptureFinished(this);
}

#if WITH_EDITOR
void UMovieSceneCapture::SaveFrameToFile(TArray<FColor> Colors)
{
	if (Colors.Num() == 0)
	{
		return;
	}

	FString Filename = ResolveUniqueFilename();
	if (Filename.IsEmpty())
	{
		return;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>( FName("ImageWrapper") );

	// clamp to 1-100 range
	const int32 ImageCompressionQuality = FMath::Clamp<int32>(Settings.CompressionQuality * 100, 1, 100);

	switch (Settings.CaptureType)
	{
	case EMovieCaptureType::BMP:
		FFileHelper::CreateBitmap(*Filename, CachedMetrics.Width, CachedMetrics.Height, Colors.GetData());
		break;

	case EMovieCaptureType::PNG:
		{
			IImageWrapperPtr ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
			for (FColor& Color : Colors)
			{
				Color.A = 255;
			}

			if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(&Colors[0], Colors.Num() * sizeof(FColor), CachedMetrics.Width, CachedMetrics.Height, ERGBFormat::BGRA, 8))
			{
				FFileHelper::SaveArrayToFile(ImageWrapper->GetCompressed(ImageCompressionQuality), *Filename);
			}
		}
		break;

	case EMovieCaptureType::JPEG:
		{
			IImageWrapperPtr ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
			if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(&Colors[0], Colors.Num() * sizeof(FColor), CachedMetrics.Width, CachedMetrics.Height, ERGBFormat::BGRA, 8))
			{
				FFileHelper::SaveArrayToFile(ImageWrapper->GetCompressed(ImageCompressionQuality), *Filename);
			}
		}
		break;

	default:
		break;
	}
}
#endif

const TCHAR* UMovieSceneCapture::GetDefaultFileExtension() const
{
	switch (Settings.CaptureType)
	{
		case EMovieCaptureType::BMP:	return TEXT(".bmp");
		case EMovieCaptureType::PNG:	return TEXT(".png");
		case EMovieCaptureType::JPEG:	return TEXT(".jpeg");
#if PLATFORM_MAC
		default:						return TEXT(".mov");
#else
		default:						return TEXT(".avi");
#endif
	}
}

FString UMovieSceneCapture::ResolveFileFormat(const FString& Folder, const FString& Format) const
{
	TMap<FString, FStringFormatArg> Mappings;
	Mappings.Add(TEXT("fps"), FString::Printf(TEXT("%d"), Settings.FrameRate));
	Mappings.Add(TEXT("frame"), FString::Printf(TEXT("%04d"), CachedMetrics.Frame));
	Mappings.Add(TEXT("width"), FString::Printf(TEXT("%d"), CachedMetrics.Width));
	Mappings.Add(TEXT("height"), FString::Printf(TEXT("%d"), CachedMetrics.Height));

	if (Settings.bUseCompression)
	{
		Mappings.Add(TEXT("quality"), FString::Printf(TEXT("%.2f"), Settings.CompressionQuality));
	}
	else
	{
		Mappings.Add(TEXT("quality"), NSLOCTEXT("MovieCapture", "Uncompressed", "Uncompressed").ToString());
	}

	return Folder / FString::Format(*Format, Mappings);
}

FString UMovieSceneCapture::ResolveUniqueFilename()
{
	FString BaseFilename = ResolveFileFormat(Settings.OutputDirectory.Path, Settings.OutputFormat);
	FString ThisTry = BaseFilename + GetDefaultFileExtension();

	if (!IFileManager::Get().DirectoryExists(*Settings.OutputDirectory.Path))
	{
		IFileManager::Get().MakeDirectory(*Settings.OutputDirectory.Path);
	}

	if (IFileManager::Get().FileSize(*ThisTry) == -1)
	{
		return ThisTry;
	}

	uint32 Index = 2;
	for (;;)
	{
		ThisTry = BaseFilename + FString::Printf(TEXT(" %02d"), Index) + GetDefaultFileExtension();

		// If the file doesn't exist, we can use that, else, increment the index and try again
		if (IFileManager::Get().FileSize(*ThisTry) == -1)
		{
			return ThisTry;
		}

		++Index;
	}

	return ThisTry;
}

FFixedTimeStepCaptureStrategy::FFixedTimeStepCaptureStrategy(uint32 InTargetFPS)
	: TargetFPS(InTargetFPS)
{
}

void FFixedTimeStepCaptureStrategy::OnStart()
{
	FApp::SetFixedDeltaTime(1.0 / TargetFPS);
	FApp::SetUseFixedTimeStep(true);
}

void FFixedTimeStepCaptureStrategy::OnStop()
{
	FApp::SetUseFixedTimeStep(false);
}

void FFixedTimeStepCaptureStrategy::OnPresent(double CurrentTimeSeconds, uint32 FrameIndex)
{
}

bool FFixedTimeStepCaptureStrategy::ShouldPresent(double CurrentTimeSeconds, uint32 FrameIndex) const
{
	return true;
}

int32 FFixedTimeStepCaptureStrategy::GetDroppedFrames(double CurrentTimeSeconds, uint32 FrameIndex) const
{
	return 0;
}

FRealTimeCaptureStrategy::FRealTimeCaptureStrategy(uint32 InTargetFPS)
	: NextPresentTimeS(0), FrameLength(1.0 / InTargetFPS)
{
}

void FRealTimeCaptureStrategy::OnStart()
{
}

void FRealTimeCaptureStrategy::OnStop()
{
}

void FRealTimeCaptureStrategy::OnPresent(double CurrentTimeSeconds, uint32 FrameIndex)
{
}

bool FRealTimeCaptureStrategy::ShouldPresent(double CurrentTimeSeconds, uint32 FrameIndex) const
{
	return CurrentTimeSeconds >= FrameIndex * FrameLength;
}

int32 FRealTimeCaptureStrategy::GetDroppedFrames(double CurrentTimeSeconds, uint32 FrameIndex) const
{
	uint32 ThisFrame = FMath::FloorToInt(CurrentTimeSeconds / FrameLength);
	if (ThisFrame > FrameIndex)
	{
		return ThisFrame - FrameIndex;
	}
	return 0;
}