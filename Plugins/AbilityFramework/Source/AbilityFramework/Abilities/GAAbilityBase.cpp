// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


#include "../AbilityFramework.h"
#include "Tasks/GAAbilityTask.h"
#include "../Effects/GAGameEffect.h"
#include "../GAGlobalTypes.h"
#include "../Effects/GAEffectGlobalTypes.h"
#include "../AFAbilityComponent.h"

#include "GameplayTagContainer.h"
#include "Net/UnrealNetwork.h"
#include "Animation/AnimMontage.h"
#include "../Effects/GABlueprintLibrary.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetSystemLibrary.h"

#include "GAAbilityBase.h"

void FGAAbilityTick::ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	if (Target && !Target->IsPendingKillOrUnreachable())
	{
		FScopeCycleCounterUObject ActorScope(Target);
		Target->TickAbility(DeltaTime, TickType, *this);
	}
}

FString FGAAbilityTick::DiagnosticMessage()
{
	return Target->GetFullName() + TEXT("[TickAction]");
}

UGAAbilityBase::UGAAbilityBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicate = true;
	bIsNameStable = false;

	TickFunction.TickGroup = TG_PrePhysics;
	// Default to no tick function, but if we set 'never ticks' to false (so there is a tick function) it is enabled by default
	TickFunction.bCanEverTick = true;
	TickFunction.bStartWithTickEnabled = true;
	TickFunction.bAllowTickOnDedicatedServer = true;
	TickFunction.bRunOnAnyThread = true;

	TickFunction.SetTickFunctionEnable(true);
}

void UGAAbilityBase::PostInitProperties()
{
	UpdateAssetRegistryInfo();
	Super::PostInitProperties();
}

void UGAAbilityBase::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		UpdateAssetRegistryInfo();
	}

	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		UpdateAssetRegistryInfo();
	}
}

#if WITH_EDITOR
void UGAAbilityBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

void UGAAbilityBase::UpdateAssetRegistryInfo()
{
	AbilityTagSearch = AbilityTag.GetTagName();
}
void UGAAbilityBase::TickAbility(float DeltaSeconds, ELevelTick TickType, FGAAbilityTick& ThisTickFunction)
{

}

void UGAAbilityBase::InitAbility()
{
	//still want to initialize, as Spec is used in multiple places.
	ActivationEffect.InitializeIfNotInitialized();
	CooldownEffect.InitializeIfNotInitialized();
	AttributeCost.InitializeIfNotInitialized();
	AbilityAttributeCost.InitializeIfNotInitialized();
	DefaultContext = UGABlueprintLibrary::MakeContext(this, POwner, AvatarActor, this, FHitResult(ForceInit));
	if (AbilityComponent)
	{
		World = AbilityComponent->GetWorld();
	}

	if (!AbilityComponent)
	{
		AbilityComponent = GetAbilityComp();
		if (AbilityComponent)
		{
			AbilityComponent->AddAddtionalAttributes(AbilityTag, Attributes);
		}
	}
	if (Attributes)
	{
		Attributes->InitializeAttributes(GetAbilityComp());
		Attributes->InitializeAttributesFromTable();
	}
	
	if (!OwnerCamera)
	{
		OwnerCamera = POwner->FindComponentByClass<UCameraComponent>();
	}
	TickFunction.Target = this;
	TickFunction.RegisterTickFunction(AbilityComponent->GetWorld()->GetCurrentLevel());
	TickFunction.SetTickFunctionEnable(true);
}

void UGAAbilityBase::OnNativeInputPressed(FGameplayTag ActionName)
{
	{
		UE_LOG(AbilityFramework, Log, TEXT("OnNativeInputPressed in ability %s"), *GetName());
		OnInputPressed(ActionName);
		OnInputPressedDelegate.Broadcast();
	}
}

void UGAAbilityBase::OnNativeInputReleased(FGameplayTag ActionName)
{
	{
		UE_LOG(AbilityFramework, Log, TEXT("OnNativeInputReleased in ability %s"), *GetName());
		OnInputReleased(ActionName);
		OnInputReleasedDelegate.Broadcast();
	}
}

void UGAAbilityBase::StartActivation(bool bApplyActivationEffect)
{
	if (!CanUseAbility())
	{
		return;
	}
	//AbilityComponent->ExecutingAbility = this;
	AbilityState = EAFAbilityState::Activating;
	NativeOnBeginAbilityActivation(bApplyActivationEffect);
}

void UGAAbilityBase::NativeOnBeginAbilityActivation(bool bApplyActivationEffect)
{
	UE_LOG(AbilityFramework, Log, TEXT("Begin Executing Ability: %s"), *GetName());

	if (OnConfirmCastingEndedDelegate.IsBound())
	{
		OnConfirmCastingEndedDelegate.Broadcast();
	}
	//ActivationInfo.SetActivationInfo();
	ApplyActivationEffect(bApplyActivationEffect);
	OnActivate();
	OnActivateBeginDelegate.Broadcast();
	AbilityComponent->AppliedTags.AddTagContainer(ActivationAddedTags);
	//OnAbilityExecuted();
}

void UGAAbilityBase::OnCooldownEffectExpired()
{
	UE_LOG(AbilityFramework, Log, TEXT("Cooldown expired In Ability: %s"), *GetName());

	if (CooldownEffectHandle.IsValid())
	{
		CooldownEffectHandle.GetContextRef().InstigatorComp->RemoveEffect(CooldownEffect, DefaultContext);
	}
}
/* Functions for activation effect delegates */
void UGAAbilityBase::NativeOnAbilityActivationFinish(const FGAEffectHandle& InHandle)
{
	UE_LOG(AbilityFramework, Log, TEXT("Ability Activation Effect Expired In Ability: %s"), *GetName());
	OnActivationFinished();
	OnActivationFinishedDelegate.Broadcast();
}
void UGAAbilityBase::NativeOnAbilityActivationCancel()
{
	//OnAbilityExecutedNative();
	//this works under assumption that current state == activation state.
	UE_LOG(AbilityFramework, Log, TEXT("Ability Activation Effect Removed In Ability: %s"), *GetName());
	//AbilityComponent->ExecutingAbility = nullptr;
	OnConfirmDelegate.Clear();
	OnConfirmDelegate.RemoveAll(this);

	OnActivationCancel();
	//AbilityActivatedCounter++;
}
void UGAAbilityBase::OnActivationEffectPeriod(const FGAEffectHandle& InHandle)
{
	UE_LOG(AbilityFramework, Log, TEXT("Ability Activation Effect Period In Ability: %s"), *GetName());

	OnPeriod();
}
void UGAAbilityBase::FinishAbility()
{
	UE_LOG(AbilityFramework, Log, TEXT("FinishExecution in ability %s"), *GetName());
	OnAbilityFinished();
	NativeFinishAbility();
	AbilityState = EAFAbilityState::Waiting;
	AbilityComponent->AppliedTags.RemoveTagContainer(ActivationAddedTags);
}
void UGAAbilityBase::NativeFinishAbility()
{
	UE_LOG(AbilityFramework, Log, TEXT("NativeFinishExecution in ability %s"), *GetName());
	AbilityComponent->ExecutingAbility = nullptr;
	OnConfirmDelegate.Clear();
	OnConfirmDelegate.RemoveAll(this);
	//if (ActivationEffect.Handle.IsValid())
	{
		AbilityComponent->RemoveEffect(ActivationEffect, DefaultContext);
	}
	//remove effect.
}
/* Functions for activation effect delegates */
void UGAAbilityBase::CancelActivation()
{
	NativeCancelActivation();
}
void UGAAbilityBase::NativeCancelActivation()
{
	UAFAbilityComponent* AttrComp = ActivationEffect.Handle.GetContext().InstigatorComp.Get();
	if (AbilityComponent)
	{
		AbilityComponent->RemoveEffect(ActivationEffect, DefaultContext);
	}
}

bool UGAAbilityBase::IsWaitingForConfirm()
{
	if (OnConfirmDelegate.IsBound())
		return true;
	else
		return false;
}
void UGAAbilityBase::ConfirmAbility()
{
	if (OnConfirmDelegate.IsBound())
		OnConfirmDelegate.Broadcast();
	OnConfirmDelegate.Clear();
	OnConfirmDelegate.RemoveAll(this);
}

bool UGAAbilityBase::ApplyCooldownEffect()
{
	if (!CooldownEffect.GetSpec())
	{
		return false;
	}
	FAFFunctionModifier Modifier;
	CooldownEffectHandle = UGABlueprintLibrary::ApplyGameEffectToObject(CooldownEffect,
		this, POwner, this, Modifier);
	OnCooldownStart();

	CooldownEffectHandle.GetEffectRef().OnEffectExpired.AddUObject(this, &UGAAbilityBase::OnCooldownEnd);
	return false;
}
bool UGAAbilityBase::ApplyActivationEffect(bool bApplyActivationEffect)
{
	if (!ActivationEffect.GetSpec())
		return false;
	FHitResult Hit(ForceInit);
	
	UGAGameEffectSpec* Spec = ActivationEffect.GetClass().GetDefaultObject();
	float DurationCheck = Spec->Duration.GetFloatValue(DefaultContext);
	float PeriodCheck = Spec->Period.GetFloatValue(DefaultContext);
	if (DurationCheck > 0 || PeriodCheck > 0)
	{
		bApplyActivationEffect = true;
	}
	
	if (bApplyActivationEffect)
	{
		FHitResult HitIn;
		if (ActivationEffectHandle.IsValid())
			ActivationEffectHandle.Reset();

		FAFFunctionModifier Modifier;
		ActivationEffectHandle = UGABlueprintLibrary::ApplyGameEffectToObject(ActivationEffect,
			this, POwner, this, Modifier);
		
		//if(!ActivationEffectHandle.GetEffectRef().OnEffectExpired.)
			ActivationEffectHandle.GetEffectRef().OnEffectExpired.AddUObject(this, &UGAAbilityBase::NativeOnAbilityActivationFinish);

		
		if (PeriodCheck > 0)
		{
			//if (!ActivationEffectHandle.GetEffectRef().OnEffectPeriod.IsBound())
			ActivationEffectHandle.GetEffectRef().OnEffectPeriod.AddUObject(this, &UGAAbilityBase::OnActivationEffectPeriod);
		}
	}
	else
	{
		NativeOnAbilityActivationFinish(FGAEffectHandle());
	}
	return false;
}

bool UGAAbilityBase::CanUseAbility()
{
	bool CanUse = true;
	bool bIsOnCooldown = IsOnCooldown();
	bool bIsActivating = IsActivating();
	//if (!AbilityComponent->ExecutingAbility)
	//	UE_LOG(AbilityFramework, Log, TEXT("CanUseAbility AbilityComponent->ExecutingAbility is true"));

	CanUse = !bIsOnCooldown && !bIsActivating;
	
	return CanUse;
}

bool UGAAbilityBase::CanReleaseAbility()
{
	bool bCanUse = true;
	/*if (AbilityComponent->ExecutingAbility == this)
	{
		bCanUse = true;
	}*/
	if (IsOnCooldown())
	{
		bCanUse = false;
		UE_LOG(AbilityFramework, Log, TEXT("CanReleaseAbility can't release ability is on cooldown"));
	}
	return bCanUse;
}

void UGAAbilityBase::OnGameplayTaskInitialized(UGameplayTask& Task)
{
	if (UGAAbilityTask* task = Cast<UGAAbilityTask>(&Task))
	{
		task->Ability = this;
	}
}
UGameplayTasksComponent* UGAAbilityBase::GetGameplayTasksComponent(const UGameplayTask& Task) const
{
	return AbilityComponent;
}
/** this gets called both when task starts and when task gets resumed. Check Task.GetStatus() if you want to differenciate */
void UGAAbilityBase::OnGameplayTaskActivated(UGameplayTask& Task)
{
	UE_LOG(AbilityFramework, Log, TEXT("Task Started; %s in ability: %s"), *Task.GetName(), *GetName());
	ActiveTasks.Add(&Task);
	//AbilityComponent->OnGameplayTaskActivated(Task);
}
/** this gets called both when task finished and when task gets paused. Check Task.GetStatus() if you want to differenciate */
void UGAAbilityBase::OnGameplayTaskDeactivated(UGameplayTask& Task)
{
	UE_LOG(AbilityFramework, Log, TEXT("Task Removed: %s in ability: %s"), *Task.GetName(), *GetName());
	ActiveTasks.Remove(&Task);
	//AbilityComponent->OnGameplayTaskDeactivated(Task);
}
AActor* UGAAbilityBase::GetGameplayTaskOwner(const UGameplayTask* Task) const
{
	return POwner;
}
AActor* UGAAbilityBase::GetGameplayTaskAvatar(const UGameplayTask* Task) const
{
	return AvatarActor;
}

class UGAAttributesBase* UGAAbilityBase::GetAttributes()
{
	return Attributes;
}
UAFAbilityComponent* UGAAbilityBase::GetAbilityComp()
{
	IAFAbilityInterface* OwnerAttributes = Cast<IAFAbilityInterface>(POwner);
	if (OwnerAttributes)
	{
		return OwnerAttributes->GetAbilityComp();
	}
	return nullptr;
}
float UGAAbilityBase::GetAttributeValue(FGAAttribute AttributeIn) const
{
	return NativeGetAttributeValue(AttributeIn);
}
float UGAAbilityBase::NativeGetAttributeValue(const FGAAttribute AttributeIn) const
{
	return Attributes->GetCurrentAttributeValue(AttributeIn);
}
float UGAAbilityBase::GetAttributeVal(FGAAttribute AttributeIn) const
{
	return Attributes->GetCurrentAttributeValue(AttributeIn);
}
FGAEffectHandle UGAAbilityBase::ApplyEffectToTarget(FGAEffect* EffectIn,
	FGAEffectProperty& InProperty, FGAEffectContext& InContext)
{ 
	return AbilityComponent->ApplyEffectToTarget(EffectIn, InProperty, InContext);
};
void UGAAbilityBase::RemoveTagContainer(const FGameplayTagContainer& TagsIn) 
{
	AbilityComponent->RemoveTagContainer(TagsIn);
};

bool UGAAbilityBase::ApplyAttributeCost()
{
	return true;
}
bool UGAAbilityBase::ApplyAbilityAttributeCost()
{
	//add checking if attribute goes below zero
	//maybe let game specific code handle it..
	FAFFunctionModifier Modifier;
	
	if (CheckAbilityAttributeCost())
	{
		AbilityAttributeCostHandle = UGABlueprintLibrary::ApplyGameEffectToObject(AbilityAttributeCost,
			this, POwner, this, Modifier);

		return true;
	}
	return false;
}
bool UGAAbilityBase::BP_ApplyAttributeCost()
{
	return ApplyAttributeCost();
}
bool UGAAbilityBase::BP_ApplyAbilityAttributeCost()
{
	return ApplyAbilityAttributeCost();
}
bool UGAAbilityBase::BP_CheckAbilityAttributeCost()
{
	return CheckAbilityAttributeCost();
}
bool UGAAbilityBase::CheckAbilityAttributeCost()
{
	float ModValue = AbilityAttributeCost.GetSpec()->AtributeModifier.Magnitude.GetFloatValue(DefaultContext);
	FGAAttribute Attribute = AbilityAttributeCost.GetSpec()->AtributeModifier.Attribute;
	float AttributeVal = Attributes->GetFloatValue(Attribute);
	if (ModValue > AttributeVal)
		return false;

	return true;
}
bool UGAAbilityBase::IsOnCooldown()
{
	bool bOnCooldown = false;
	bOnCooldown = AbilityComponent->IsEffectActive(CooldownEffectHandle);
	if (bOnCooldown)
	{
		OnNotifyOnCooldown.Broadcast();
	}
	return bOnCooldown; //temp
}
bool UGAAbilityBase::IsActivating()
{
	bool bAbilityActivating = false;
	bAbilityActivating = AbilityComponent->IsEffectActive(ActivationEffect.Handle) || AbilityState == EAFAbilityState::Activating;
	return bAbilityActivating; //temp
}
void UGAAbilityBase::BP_ApplyCooldown()
{
	ApplyCooldownEffect();
}

float UGAAbilityBase::GetCurrentActivationTime()
{
	if (ActivationEffectHandle.IsValid())
	{
		return ActivationEffectHandle.GetEffectPtr()->GetCurrentActivationTime();
	}
	return 0;
}

float UGAAbilityBase::CalculateAnimationSpeed(UAnimMontage* MontageIn)
{
	float ActivationTime = MontageIn->GetPlayLength();
	UGAGameEffectSpec* Spec = ActivationEffect.GetClass().GetDefaultObject();
	float DurationCheck = Spec->Duration.GetFloatValue(DefaultContext);
	if (DurationCheck > 0)
	{
		ActivationTime = DurationCheck;
	}
	float Duration = MontageIn->GetPlayLength();
	
	float PlaySpeed = Duration / ActivationTime;
	return PlaySpeed;
}


bool UGAAbilityBase::IsNameStableForNetworking() const
{
	return bIsNameStable;
}

void UGAAbilityBase::SetNetAddressable()
{
	bIsNameStable = true;
}

class UWorld* UGAAbilityBase::GetWorld() const
{
	return World;
}
void UGAAbilityBase::PlayMontage(UAnimMontage* MontageIn, FName SectionName, float Speed)
{
	AbilityComponent->PlayMontage(MontageIn, SectionName, Speed);
}

//replication
void UGAAbilityBase::GetLifetimeReplicatedProps(TArray< class FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	//possibly can be infered apon replication.
	DOREPLIFETIME(UGAAbilityBase, POwner);
	DOREPLIFETIME(UGAAbilityBase, Character);
	DOREPLIFETIME(UGAAbilityBase, PCOwner);
	DOREPLIFETIME(UGAAbilityBase, AICOwner);
	//probabaly should be SKIP_Owner, and I'm not really sure if Multicast wouldn't be better.

	DOREPLIFETIME(UGAAbilityBase, AvatarActor)
	//DOREPLIFETIME(UAFAbilityComponent, RepMontage);
}

void UGAAbilityBase::ExecuteAbilityInputPressedFromTag(FGameplayTag AbilityTagIn, FGameplayTag ActionName)
{
	AbilityComponent->NativeInputPressed(ActionName);
}
void UGAAbilityBase::ExecuteAbilityInputReleasedFromTag(FGameplayTag AbilityTagIn, FGameplayTag ActionName)
{
	AbilityComponent->NativeInputReleased(ActionName);
}

/* Tracing Helpers Start */
bool UGAAbilityBase::LineTraceSingleByChannel(const FVector Start, const FVector End, ETraceTypeQuery TraceChannel, bool bTraceComplex, FHitResult& OutHit)
{
	ECollisionChannel CollisionChannel = UEngineTypes::ConvertToCollisionChannel(TraceChannel);
	static const FName LineTraceSingleName(TEXT("AbilityLineTraceSingle"));
	FCollisionQueryParams Params(LineTraceSingleName, bTraceComplex);

	bool bHit = World->LineTraceSingleByChannel(OutHit, Start, End, CollisionChannel, Params);
	return bHit;
}
bool UGAAbilityBase::LineTraceSingleByChannelFromCamera(float Range, ETraceTypeQuery TraceChannel, bool bTraceComplex, FHitResult& OutHit,
	EDrawDebugTrace::Type DrawDebugType, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime)
{
	FVector Start = FVector::ZeroVector;
	if (OwnerCamera)
	{
		Start = OwnerCamera->GetComponentLocation();
	}
	else
	{
		FRotator UnusedRot;
		POwner->GetActorEyesViewPoint(Start, UnusedRot);
	}
	FVector End = (POwner->GetBaseAimRotation().Vector() * Range) + Start;
	ECollisionChannel CollisionChannel = UEngineTypes::ConvertToCollisionChannel(TraceChannel);
	static const FName LineTraceSingleName(TEXT("AbilityLineTraceSingle"));
	FCollisionQueryParams Params(LineTraceSingleName, bTraceComplex);
	bool bHit = World->LineTraceSingleByChannel(OutHit, Start, End, CollisionChannel, Params);
#if ENABLE_DRAW_DEBUG
	if (DrawDebugType != EDrawDebugTrace::None)
	{
		bool bPersistent = DrawDebugType == EDrawDebugTrace::Persistent;
		float LifeTime = (DrawDebugType == EDrawDebugTrace::ForDuration) ? DrawTime : 0.f;

		// @fixme, draw line with thickness = 2.f?
		if (bHit && OutHit.bBlockingHit)
		{
			// Red up to the blocking hit, green thereafter
			::DrawDebugLine(World, Start, OutHit.ImpactPoint, TraceColor.ToFColor(true), bPersistent, LifeTime);
			::DrawDebugLine(World, OutHit.ImpactPoint, End, TraceHitColor.ToFColor(true), bPersistent, LifeTime);
			::DrawDebugPoint(World, OutHit.ImpactPoint, 16, TraceColor.ToFColor(true), bPersistent, LifeTime);
		}
		else
		{
			// no hit means all red
			::DrawDebugLine(World, Start, End, TraceColor.ToFColor(true), bPersistent, LifeTime);
		}
	}
#endif
	return bHit;
}
bool UGAAbilityBase::LineTraceSingleByChannelFromSocket(FName SocketName, float Range, ETraceTypeQuery TraceChannel, bool bTraceComplex, FHitResult& OutHit,
	EDrawDebugTrace::Type DrawDebugType, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime)
{
	return false;
}
bool UGAAbilityBase::LineTraceSingleByChannelCorrected(FName SocketName, float Range, ETraceTypeQuery TraceChannel, bool bTraceComplex, FHitResult& OutHit,
	EDrawDebugTrace::Type DrawDebugType, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime)
{
	return false;
}
/* Tracing Helpers End */

//Helpers
float UGAAbilityBase::GetActivationRemainingTime() const
{
	return AbilityComponent->GameEffectContainer.GetRemainingTime(ActivationEffectHandle);
}
float UGAAbilityBase::GetActivationRemainingTimeNormalized() const
{
	return AbilityComponent->GameEffectContainer.GetRemainingTimeNormalized(ActivationEffectHandle);
}
float UGAAbilityBase::GetActivationCurrentTime() const
{
	return AbilityComponent->GameEffectContainer.GetCurrentTime(ActivationEffectHandle);
}
float UGAAbilityBase::GetActivationCurrentTimeNormalized() const
{
	return AbilityComponent->GameEffectContainer.GetCurrentTimeNormalized(ActivationEffectHandle);
}
float UGAAbilityBase::GetActivationEndTime() const
{
	return AbilityComponent->GameEffectContainer.GetEndTime(ActivationEffectHandle);
}
float UGAAbilityBase::BP_GetActivationRemainingTime()
{
	return GetActivationRemainingTime();
}
float UGAAbilityBase::BP_GetActivationRemainingTimeNormalized()
{
	return GetActivationRemainingTimeNormalized();
}
float UGAAbilityBase::BP_GetActivationCurrentTime()
{
	return GetActivationCurrentTime();
}
float UGAAbilityBase::BP_GetActivationCurrentTimeNormalized()
{
	return GetActivationCurrentTimeNormalized();
}
float UGAAbilityBase::BP_GetActivationEndTime()
{
	return GetActivationEndTime();
}


float UGAAbilityBase::GetCooldownRemainingTime() const
{
	return AbilityComponent->GameEffectContainer.GetRemainingTime(CooldownEffectHandle);
}
float UGAAbilityBase::GetCooldownRemainingTimeNormalized() const
{
	return AbilityComponent->GameEffectContainer.GetRemainingTimeNormalized(CooldownEffectHandle);
}
float UGAAbilityBase::GetCooldownCurrentTime() const
{
	return AbilityComponent->GameEffectContainer.GetCurrentTime(CooldownEffectHandle);
}
float UGAAbilityBase::GetCooldownCurrentTimeNormalized() const
{
	return AbilityComponent->GameEffectContainer.GetCurrentTimeNormalized(CooldownEffectHandle);
}
float UGAAbilityBase::GetCooldownEndTime() const
{
	return AbilityComponent->GameEffectContainer.GetEndTime(CooldownEffectHandle);
}
float UGAAbilityBase::BP_GetCooldownRemainingTime()
{
	return GetCooldownRemainingTime();
}
float UGAAbilityBase::BP_GetCooldownRemainingTimeNormalized()
{
	return GetCooldownRemainingTimeNormalized();
}
float UGAAbilityBase::BP_GetCooldownCurrentTime()
{
	return GetCooldownCurrentTime();
}
float UGAAbilityBase::BP_GetCooldownCurrentTimeNormalized()
{
	return GetCooldownCurrentTimeNormalized();
}
float UGAAbilityBase::BP_GetCooldownEndTime()
{
	return GetCooldownEndTime();
}