#include "SIUnitComponent.h"
#include "Animation/AnimSequence.h"
#include "SIMeshComponent.h"

#pragma optimize( "", off )

class USIUnitComponent::FAnimtionPlayer
{
public:
	struct Sequence
	{
		int Id;
		float Time;
		float Length;
		int NumFrames;

		Sequence() : Id(-1), Time(0), Length(0), NumFrames(0) {}
		Sequence(int Id, float Length, int NumFrames) : Id(Id), Time(0), Length(Length), NumFrames(NumFrames) {}

		void Tick(float DeltaTime, bool Loop = false)
		{
			Time += DeltaTime;
			if (Loop)
				Time = FMath::Fmod(Time, Length);
			else
				Time = FMath::Min(Time, Length);
		}
	};

public:
	const Sequence& GetCurrentSeq() const { return CurrentSeq; }
	const Sequence& GetNextSeq() const { return NextSeq; }
	float GetFadeTime() const { return FadeTime; }
	float GetFadeLength() const { return FadeLength; }

public:
	void Tick(float DeltaTime)
	{
		CurrentSeq.Tick(DeltaTime, IsLoop);

		if (FadeTime > 0 && FadeLength > 0)
		{
			FadeTime = FMath::Max(FadeTime - DeltaTime, 0.0f);

			NextSeq.Tick(DeltaTime);

			if (FadeTime <= 0)
			{
				CurrentSeq = NextSeq;
			}
		}
	}

	void Play(const Sequence& Seq, bool Loop)
	{
		IsLoop = Loop;
		CurrentSeq = NextSeq = Seq;
		FadeLength = FadeTime = 0;
	}

	void CrossFade(const Sequence& Seq, bool Loop, float Fade)
	{
		if (CurrentSeq.Id < 0)
		{
			Play(Seq, Loop);
			return;
		}
		NextSeq = Seq;
		FadeLength = FadeTime = Fade;
	}

private:
	bool IsLoop = false;
	float FadeLength = 0;

private:
	Sequence CurrentSeq;
	Sequence NextSeq;
	float FadeTime = 0;
};

namespace
{
	void GetInstanceDataFromPlayer(FInstancedSkinnedMeshInstanceData::FAnimData& Data, 
		const USIUnitComponent::FAnimtionPlayer::Sequence& Seq)
	{
		int NumFrames = Seq.NumFrames;
		float SequenceLength = Seq.Length;
		float Interval = (NumFrames > 1) ? (SequenceLength / (NumFrames - 1)) : MINIMUM_ANIMATION_LENGTH;

		float Time = Seq.Time;
		int Frame = Time / Interval;
		float Lerp = (Time - Frame * Interval) / Interval;

		Data.Sequence = Seq.Id;
		Data.PrevFrame = FMath::Clamp(Frame, 0, NumFrames - 1);
		Data.NextFrame = FMath::Clamp(Frame + 1, 0, NumFrames - 1);
		Data.FrameLerp = FMath::Clamp(Lerp, 0.0f, 1.0f);
	}
}

USIUnitComponent::USIUnitComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bAutoActivate = true;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;

	InstanceId = 0;

	AnimtionPlayer = new FAnimtionPlayer();
}

USIUnitComponent::~USIUnitComponent()
{
	delete AnimtionPlayer;
}

void USIUnitComponent::BeginPlay()
{
	Super::BeginPlay();
}

void USIUnitComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (InstanceId > 0 && InstanceManagerObject.IsValid())
	{
		USIMeshComponent* InstanceManager = Cast<USIMeshComponent>(
			InstanceManagerObject->GetComponentByClass(USIMeshComponent::StaticClass())
			);
		if (InstanceManager)
		{
			InstanceManager->RemoveInstance(InstanceId);
		}
	}
}

void USIUnitComponent::CrossFade(int Sequence, float FadeLength, bool Loop)
{
	if (InstanceManagerObject.IsValid())
	{
		USIMeshComponent* InstanceManager = Cast<USIMeshComponent>(
			InstanceManagerObject->GetComponentByClass(USIMeshComponent::StaticClass())
			);
		if (InstanceManager)
		{
			UAnimSequence* AnimSequence = InstanceManager->GetSequence(Sequence);
			if (AnimSequence)
			{
				int NumFrames = AnimSequence->GetNumberOfFrames();
				FAnimtionPlayer::Sequence Seq(Sequence, AnimSequence->SequenceLength, NumFrames);
				AnimtionPlayer->CrossFade(Seq, Loop, FadeLength);
			}
		}
	}
}

void USIUnitComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction * ThisTickFunction)
{
	// Tick ActorComponent first.
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (InstanceId <= 0 && InstanceManagerObject.IsValid())
	{
		USIMeshComponent* InstanceManager = Cast<USIMeshComponent>(
			InstanceManagerObject->GetComponentByClass(USIMeshComponent::StaticClass())
			);
		if (InstanceManager)
		{
			InstanceId = InstanceManager->AddInstance(GetComponentTransform());
		}
	}

	AnimtionPlayer->Tick(DeltaTime);

	if (InstanceManagerObject.IsValid() && InstanceId > 0)
	{
		USIMeshComponent* InstanceManager = Cast<USIMeshComponent>(
			InstanceManagerObject->GetComponentByClass(USIMeshComponent::StaticClass())
			);
		if (InstanceManager)
		{
			FInstancedSkinnedMeshInstanceData* Instance = InstanceManager->GetInstanceData(InstanceId);
			check(Instance);
			Instance->Transform = GetComponentTransform().ToMatrixWithScale();

			GetInstanceDataFromPlayer(Instance->AnimDatas[0], AnimtionPlayer->GetCurrentSeq());
			GetInstanceDataFromPlayer(Instance->AnimDatas[1], AnimtionPlayer->GetNextSeq());

			float BlendWeight = 1;

			if (AnimtionPlayer->GetCurrentSeq().Id != AnimtionPlayer->GetNextSeq().Id)
			{
				float FadeTime = AnimtionPlayer->GetFadeTime();
				float FadeLength = FMath::Max(AnimtionPlayer->GetFadeLength(), 0.001f);
				BlendWeight = FadeTime / FadeLength;
			}

			Instance->AnimDatas[0].BlendWeight = BlendWeight;
			Instance->AnimDatas[1].BlendWeight = 1 - BlendWeight;
		}
	}
}

#pragma optimize( "", on )
