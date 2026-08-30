// Copyright Solessfir 2026. All Rights Reserved.

#include "LoreSourceControlState.h"
#include "LoreSourceControlUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString MakeTestPath(const FString& Root, const FString& RelativePath)
	{
		FString Path = FPaths::Combine(Root, RelativePath);
		FPaths::NormalizeFilename(Path);
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Path;
	}

	const FLoreSourceControlState* FindState(const TArray<FLoreSourceControlState>& States, const FString& Filename)
	{
		return States.FindByPredicate([&Filename](const FLoreSourceControlState& State) { return State.LocalFilename == Filename; });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoreStatusParserTest, "LoreSourceControl.Status.Parser", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoreStatusParserTest::RunTest(const FString& Parameters)
{
	const FString RepositoryRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LoreSourceControlTests")));
	const FString ModifiedPath = MakeTestPath(RepositoryRoot, TEXT("Content/Modified.uasset"));
	const FString AddedPath = MakeTestPath(RepositoryRoot, TEXT("Content/Added.uasset"));
	const FString MovedPath = MakeTestPath(RepositoryRoot, TEXT("Content/Moved.uasset"));
	const FString OldPath = MakeTestPath(RepositoryRoot, TEXT("Content/Old.uasset"));
	const FString IgnoredPath = MakeTestPath(RepositoryRoot, TEXT("Saved/Ignored.txt"));
	const FString ExcludedPath = MakeTestPath(RepositoryRoot, TEXT("Saved/Excluded.txt"));
	const FString CleanPath = MakeTestPath(RepositoryRoot, TEXT("Content/Clean.uasset"));

	const FString Results = FString::Join(TArray<FString>{
		TEXT(R"({"tagName":"repositoryStatusRevision","data":{"branchName":"main","isRemoteAhead":1,"isLocalAhead":0,"remoteAvailable":1}})"),
		TEXT(R"({"tagName":"repositoryStatusFile","data":{"path":"Content/Modified.uasset","action":"keep","type":"file","flagDirty":1,"flagStaged":0,"flagConflict":0,"flagConflictUnresolved":0}})"),
		TEXT(R"({"tagName":"repositoryStatusFile","data":{"path":"Content/Added.uasset","action":"add","type":"file","flagDirty":1,"flagStaged":1,"flagConflict":0,"flagConflictUnresolved":0}})"),
		TEXT(R"({"tagName":"repositoryStatusFile","data":{"path":"Content/Moved.uasset","fromPath":"Content/Old.uasset","action":"move","type":"file","flagDirty":1,"flagStaged":0,"flagConflict":0,"flagConflictUnresolved":0}})"),
		TEXT(R"({"tagName":"repositoryStatusFile","data":{"path":"Content/Folder","action":"add","type":"directory","flagDirty":1,"flagStaged":0,"flagConflict":0,"flagConflictUnresolved":0}})"),
		TEXT(R"({"tagName":"pathIgnore","data":{"path":"Saved/Ignored.txt"}})"),
		TEXT(R"({"tagName":"filterExclude","data":{"reason":0,"path":"Saved/Excluded.txt"}})"),
		TEXT("not json")
	}, TEXT("\n"));

	TArray<FLoreSourceControlState> States;
	FLoreStatusSummary Summary;
	FLoreSourceControlUtils::ParseStatusResults(Results, TArray<FString>{ IgnoredPath, ExcludedPath, CleanPath }, RepositoryRoot, States, &Summary);

	TestEqual(TEXT("Branch name"), Summary.BranchName, FString(TEXT("main")));
	TestTrue(TEXT("Remote-ahead flag"), Summary.bIsRemoteAhead);
	TestFalse(TEXT("Local-ahead flag"), Summary.bIsLocalAhead);
	TestTrue(TEXT("Remote reported reachable"), Summary.bRemoteAvailable);
	TestEqual(TEXT("State count"), States.Num(), 7);

	const FLoreSourceControlState* Modified = FindState(States, ModifiedPath);
	const FLoreSourceControlState* Added = FindState(States, AddedPath);
	const FLoreSourceControlState* Moved = FindState(States, MovedPath);
	const FLoreSourceControlState* Old = FindState(States, OldPath);
	const FLoreSourceControlState* Ignored = FindState(States, IgnoredPath);
	const FLoreSourceControlState* Excluded = FindState(States, ExcludedPath);
	const FLoreSourceControlState* Clean = FindState(States, CleanPath);

	TestTrue(TEXT("Modified file parsed"), Modified && Modified->bIsModified && Modified->bCanCheckIn);
	TestTrue(TEXT("Added file parsed"), Added && Added->bIsAdded && Added->bIsStaged);
	TestTrue(TEXT("Move destination parsed"), Moved && Moved->bIsAdded);
	TestTrue(TEXT("Move source parsed"), Old && Old->bIsDeleted);
	TestTrue(TEXT("Ignored file parsed"), Ignored && Ignored->bIsIgnored && !Ignored->bIsSourceControlled);
	// 0.8.6 renamed the event, and an unrecognized one would silently offer an excluded path for add.
	TestTrue(TEXT("Excluded file parsed (0.8.6 filterExclude)"), Excluded && Excluded->bIsIgnored && !Excluded->bIsSourceControlled);
	TestTrue(TEXT("Clean requested file parsed"), Clean && Clean->bIsSourceControlled && Clean->bIsCurrent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoreHistoryParserTest, "LoreSourceControl.History.Parser", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoreHistoryParserTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Results{
		TEXT(R"({"tagName":"fileHistory","data":{"revision":"abc123","revisionNumber":7,"size":42,"action":"add"}})"),
		TEXT(R"({"tagName":"metadata","data":{"key":"message","value":{"data":"Initial asset"}}})"),
		TEXT(R"({"tagName":"metadata","data":{"key":"created-by","value":{"data":"Solessfir"}}})"),
		TEXT(R"({"tagName":"metadata","data":{"key":"timestamp","value":{"data":1700000000000}}})"),
		TEXT(R"({"tagName":"fileHistory","data":{"revision":"def456","revisionNumber":8,"size":84,"action":"keep"}})"),
		TEXT("not json")
	};

	FLoreSourceControlHistory History;
	FLoreSourceControlUtils::ParseHistoryResults(Results, TEXT("lore"), TEXT("C:/Repo"), TEXT("C:/Repo/Content/Test.uasset"), History);

	TestEqual(TEXT("History count"), History.Num(), 2);
	TestEqual(TEXT("First revision hash"), History[0]->RevisionHash, FString(TEXT("abc123")));
	TestEqual(TEXT("First revision number"), History[0]->RevisionNumber, 7);
	TestEqual(TEXT("First description"), History[0]->Description, FString(TEXT("Initial asset")));
	TestEqual(TEXT("First author"), History[0]->UserName, FString(TEXT("Solessfir")));
	TestEqual(TEXT("First action"), History[0]->Action, FString(TEXT("Add")));
	TestEqual(TEXT("First timestamp"), History[0]->Date.ToUnixTimestamp(), static_cast<int64>(1700000000));
	TestEqual(TEXT("Second action"), History[1]->Action, FString(TEXT("Edit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoreBranchParserTest, "LoreSourceControl.Branches.Parser", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoreBranchParserTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Results{
		TEXT(R"({"tagName":"branchListEntry","data":{"name":"main","isCurrent":0}})"),
		TEXT(R"({"tagName":"branchListEntry","data":{"name":"feature","isCurrent":0}})"),
		TEXT(R"({"tagName":"branchListEntry","data":{"name":"main","isCurrent":1}})"),
		TEXT(R"({"tagName":"branchListEntry","data":{"name":"","isCurrent":0}})")
	};

	TArray<FLoreBranchInfo> Branches;
	FLoreSourceControlUtils::ParseBranchResults(Results, Branches);

	TestEqual(TEXT("Unique branch count"), Branches.Num(), 2);
	const FLoreBranchInfo* Main = Branches.FindByPredicate([](const FLoreBranchInfo& Branch) { return Branch.Name == TEXT("main"); });
	const FLoreBranchInfo* Feature = Branches.FindByPredicate([](const FLoreBranchInfo& Branch) { return Branch.Name == TEXT("feature"); });
	TestTrue(TEXT("Current branch merged from duplicate entries"), Main && Main->bIsCurrent);
	TestTrue(TEXT("Feature branch parsed"), Feature && !Feature->bIsCurrent);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoreLockParserTest, "LoreSourceControl.Locks.Parser", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoreLockParserTest::RunTest(const FString& Parameters)
{
	const FString RepositoryRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LoreSourceControlTests")));
	const TArray<FString> Results{
		TEXT(R"({"tagName":"lockFileQuery","data":{"path":"Content/Owned.uasset","owner":"user-1"}})"),
		TEXT(R"({"tagName":"lockFileQuery","data":{"path":"Content/Raw.uasset","owner":"user-2"}})"),
		TEXT(R"({"tagName":"authUserInfo","data":{"id":"user-1","name":"Alice"}})")
	};

	TMap<FString, FLoreLockOwner> Locks;
	FLoreSourceControlUtils::ParseLockResults(Results, RepositoryRoot, Locks);

	const FLoreLockOwner* NamedOwner = Locks.Find(MakeTestPath(RepositoryRoot, TEXT("Content/Owned.uasset")));
	const FLoreLockOwner* RawOwner = Locks.Find(MakeTestPath(RepositoryRoot, TEXT("Content/Raw.uasset")));
	TestEqual(TEXT("Lock count"), Locks.Num(), 2);
	TestTrue(TEXT("Resolved owner parsed"), NamedOwner && NamedOwner->Identity == TEXT("user-1") && NamedOwner->GetDisplayName() == TEXT("Alice"));
	TestTrue(TEXT("Raw owner fallback parsed"), RawOwner && RawOwner->GetDisplayName() == TEXT("user-2"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoreCommandErrorParserTest, "LoreSourceControl.Commands.ErrorParser", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoreCommandErrorParserTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Results{
		TEXT(R"({"tagName":"complete","data":{"status":0,"error":{"errorCode":0,"message":""}}})"),
		TEXT(R"({"tagName":"log","data":{"level":"warning","message":"ignored warning"}})"),
		TEXT(R"({"tagName":"log","data":{"level":"error","message":"authentication requires a configured auth endpoint"}})"),
		TEXT(R"({"tagName":"complete","data":{"status":18,"error":{"errorCode":18,"message":"authentication requires a configured auth endpoint"}}})"),
		TEXT("not json")
	};

	TArray<FString> Errors;
	FLoreSourceControlUtils::ParseCommandErrors(Results, Errors);
	TestEqual(TEXT("Structured error count"), Errors.Num(), 2);

	TArray<FString> SuccessfulLockErrors = Errors;
	SuccessfulLockErrors.Add(TEXT("lore: unrelated failure"));
	FLoreSourceControlUtils::RemoveOptionalLockQueryErrors(true, SuccessfulLockErrors);
	TestEqual(TEXT("Successful lock query keeps only unrelated errors"), SuccessfulLockErrors.Num(), 1);
	TestEqual(TEXT("Unrelated error remains"), SuccessfulLockErrors[0], FString(TEXT("lore: unrelated failure")));

	TArray<FString> FailedLockErrors = Errors;
	FLoreSourceControlUtils::RemoveOptionalLockQueryErrors(false, FailedLockErrors);
	TestEqual(TEXT("Failed lock query retains auth errors"), FailedLockErrors.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoreRemoteReachabilityTest, "LoreSourceControl.Status.RemoteReachability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoreRemoteReachabilityTest::RunTest(const FString& Parameters)
{
	const FString RepositoryRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LoreSourceControlTests")));

	// Lore 0.8.6 reports these as numbers, not JSON booleans.
	FLoreStatusSummary Unreachable;
	TArray<FLoreSourceControlState> UnreachableStates;
	FLoreSourceControlUtils::ParseStatusResults(
		TEXT(R"({"tagName":"repositoryStatusRevision","data":{"branchName":"main","remoteAvailable":0,"remoteAuthorized":0,"remoteBranchExist":0}})"),
		TArray<FString>(), RepositoryRoot, UnreachableStates, &Unreachable);
	TestFalse(TEXT("remoteAvailable 0 means out of reach"), Unreachable.bRemoteAvailable);

	// An older CLI omits the field entirely; the default must keep the previous always-online behaviour.
	FLoreStatusSummary Absent;
	TArray<FLoreSourceControlState> AbsentStates;
	FLoreSourceControlUtils::ParseStatusResults(
		TEXT(R"({"tagName":"repositoryStatusRevision","data":{"branchName":"main"}})"),
		TArray<FString>(), RepositoryRoot, AbsentStates, &Absent);
	TestTrue(TEXT("Missing remoteAvailable defaults to reachable"), Absent.bRemoteAvailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoreLockServiceUnavailableTest, "LoreSourceControl.Locks.ServiceUnavailable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoreLockServiceUnavailableTest::RunTest(const FString& Parameters)
{
	// Verbatim from "lore --json lock query" against a repository whose remote is unreachable.
	// The wording is not stable across Lore versions - 0.8.5 reports "Disconnected from server" where an
	// earlier build reported "while offline" - and the "complete" event carries the bare message with no
	// trace to key off, so every form that has been seen in the wild is pinned here.
	const TArray<FString> Results =
	{
		TEXT(R"({"tagName":"log","data":{"level":"error","message":"Unable to check lock status while offline: gRPC connection to https://localhost/: transport error"}})"),
		TEXT(R"({"tagName":"complete","data":{"status":-1,"error":{"errorCode":-1,"message":"No auth endpoint available"}}})"),
		TEXT(R"({"tagName":"complete","data":{"status":6,"error":{"errorCode":6,"message":"Disconnected from server"}}})")
	};

	TArray<FString> Errors;
	FLoreSourceControlUtils::ParseCommandErrors(Results, Errors);
	TestEqual(TEXT("Offline lock query error count"), Errors.Num(), 3);

	for (const FString& Error : Errors)
	{
		TestTrue(FString::Printf(TEXT("Recognized as lock service unavailable: %s"), *Error), FLoreSourceControlUtils::IsLockServiceUnavailableError(Error));
	}

	TestFalse(TEXT("An unrelated failure is not a lock service outage"), FLoreSourceControlUtils::IsLockServiceUnavailableError(TEXT("lore: repository is corrupt")));

	// A successful query still drops the optional owner-name lookup failure it leaves behind.
	TArray<FString> AfterSuccess = Errors;
	FLoreSourceControlUtils::RemoveOptionalLockQueryErrors(true, AfterSuccess);
	TestEqual(TEXT("Successful query discards unavailable-class errors"), AfterSuccess.Num(), 0);

	// A failed query keeps them - GetLoreLockStatus, not this helper, decides when an outage stops being reported.
	TArray<FString> AfterFailure = Errors;
	FLoreSourceControlUtils::RemoveOptionalLockQueryErrors(false, AfterFailure);
	TestEqual(TEXT("Failed query leaves the errors for the caller to classify"), AfterFailure.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoreChangedPathClassifierTest, "LoreSourceControl.Paths.Classifier", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLoreChangedPathClassifierTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Paths{
		TEXT("Content/Asset.uasset"),
		TEXT("Plugins/Example/Content/Icon.png"),
		TEXT("Source/Game/Game.cpp"),
		TEXT("Config/DefaultEngine.ini"),
		TEXT("Plugins/Example/Example.uplugin")
	};

	TArray<FString> ContentPaths;
	const bool bRequiresRestart = FLoreSourceControlUtils::ClassifyChangedPaths(Paths, ContentPaths);
	TestTrue(TEXT("Code and configuration require restart"), bRequiresRestart);
	TestEqual(TEXT("Reloadable content path count"), ContentPaths.Num(), 2);
	TestEqual(TEXT("First content path"), ContentPaths[0], FString(TEXT("Content/Asset.uasset")));
	TestEqual(TEXT("Second content path"), ContentPaths[1], FString(TEXT("Plugins/Example/Content/Icon.png")));
	return true;
}

#endif
