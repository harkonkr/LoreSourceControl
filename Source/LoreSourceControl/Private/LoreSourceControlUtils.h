// Copyright Solessfir 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LoreSourceControlRevision.h"

class FLoreSourceControlState;
class FLoreSourceControlProvider;

/** One entry from "lore branch list" */
struct LORESOURCECONTROL_API FLoreBranchInfo
{
	FString Name;
	bool bIsCurrent = false;
};

/** Repository-level facts extracted from one "lore status" run, alongside the per-file states. */
struct LORESOURCECONTROL_API FLoreStatusSummary
{
	/** Current branch, from the "repositoryStatusRevision" event (empty if not reported) */
	FString BranchName;

	/** The remote is ahead of (or has diverged from) the local branch - a sync is needed */
	bool bIsRemoteAhead = false;

	/** The local branch contains commits that have not been published yet. */
	bool bIsLocalAhead = false;

	/**
	 * Lore reached the remote during this status call ("remoteAvailable", Lore 0.8.6+).
	 * Defaults to true so an older CLI that omits the field behaves exactly as before.
	 */
	bool bRemoteAvailable = true;
};

/** Outcome of a "lore lock query", which can fail for reasons that are not the caller's problem. */
enum class ELoreLockQueryResult : uint8
{
	/** Locks were listed - OutLockedBy is authoritative. */
	Succeeded,

	/**
	 * The lock service could not be reached (remote unreachable, or no auth endpoint configured).
	 * Lock ownership is simply unknown; everything Lore reports purely from the working copy is still valid.
	 */
	Unavailable,

	/** The query failed for some other reason and the errors are worth surfacing. */
	Failed
};

/** A Lore lock owner, retaining both the stable identity and its human-readable name. */
struct LORESOURCECONTROL_API FLoreLockOwner
{
	FString Identity;
	FString DisplayName;

	FString GetDisplayName() const
	{
		return DisplayName.IsEmpty() ? Identity : DisplayName;
	}

	/**
	 * True when Lore could not resolve who holds the lock.
	 *
	 * A server with no auth endpoint configured answers `lock query` with the literal owner
	 * "<unknown>" and logs "authentication requires a configured auth endpoint"; the raw user ID
	 * never becomes an identity we could compare against. Measured on lore 0.10.0+1172,
	 * 2026-10-04.
	 */
	bool IsUnresolved() const
	{
		return Identity.IsEmpty()
			|| Identity.Equals(TEXT("<unknown>"), ESearchCase::IgnoreCase)
			|| Identity.Equals(TEXT("unknown"), ESearchCase::IgnoreCase);
	}

	/**
	 * Whether this lock should be treated as ours.
	 *
	 * "me"/"self" are Lore's own aliases and the configured identity is this repository's. An
	 * UNRESOLVED owner also counts as ours, deliberately: on a server without auth every lock
	 * reads as "<unknown>", so refusing on it blocks every submit forever - including locks this
	 * very editor just took. The cost of being wrong is bounded, because Lore locks are advisory
	 * and a real push still has to succeed. A resolved identity that differs is still refused, so
	 * a server that does have auth keeps full multi-user protection.
	 */
	bool IsOwnedBy(const FString& InOwnIdentity) const
	{
		return IsUnresolved()
			|| Identity.Equals(TEXT("me"), ESearchCase::IgnoreCase)
			|| Identity.Equals(TEXT("self"), ESearchCase::IgnoreCase)
			|| (!InOwnIdentity.IsEmpty() && Identity.Equals(InOwnIdentity, ESearchCase::IgnoreCase));
	}
};

namespace FLoreSourceControlUtils
{
	/** Quote one command-line argument without changing its value. */
	LORESOURCECONTROL_API FString QuoteCommandLineArgument(const FString& InArgument);

	/**
	 * Returns the effective path (or command name) to use for the lore executable.
	 *
	 * Priority:
	 *   1. User-specified path from ULoreSourceControlSettings (Project Settings)
	 *   2. "lore" / "lore.exe" if available via system PATH (tried by executing)
	 *   3. Default install location: C:/Program Files/lore/lore.exe (and variants)
	 *   4. Bare "lore.exe" as last resort
	 */
	LORESOURCECONTROL_API FString FindLoreBinaryPath();

	/**
	 * Read the top-level "remote_url" and "identity" scalars out of ".lore/config.toml".
	 * Returns false only if the file could not be read; either output may still be empty
	 * (e.g. identity is unset until the first commit, or the repo was created fully offline).
	 */
	bool ReadRepositoryConfig(const FString& InRepositoryRoot, FString& OutRemoteUrl, FString& OutIdentity);

	/** Run "lore --version", parse the reported version, and indicate whether it is in the tested range. */
	LORESOURCECONTROL_API bool CheckLoreAvailability(const FString& InLoreBinaryPath, FString* OutVersion = nullptr, bool* OutTestedVersion = nullptr);

	/**
	 * Find the lore repository root by walking up looking for a ".lore" directory.
	 */
	bool FindRootDirectory(const FString& InPath, FString& OutRepositoryRoot);

	/**
	 * Run a lore command and capture output.
	 * The command is executed with working directory = RepositoryRoot if provided.
	 * bUseJson defaults to true because every internal caller parses structured events out of
	 * OutResults - pass false only to get lore's own human-readable text (e.g. for printing status
	 * straight to the log), never for a result this module intends to parse itself.
	 */
	LORESOURCECONTROL_API bool RunLoreCommand(const FString& InCommand, const FString& InLoreBinary, const FString& InRepositoryRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages, bool bUseJson = true);

	/**
	 * Run "lore status" (with --scan recommended for accuracy) and parse results into states.
	 *
	 * InProvider must be the caller's provider, captured on the game thread (FLoreSourceControlCommand::Provider) -
	 * this can run on a background thread, where looking the provider up via FModuleManager/ISourceControlModule
	 * has raced the game thread and deadlocked (observed hanging indefinitely on new-asset creation).
	 */
	bool RunUpdateStatus(const FString& InLoreBinary, const FString& InRepositoryRoot, const TArray<FString>& InFiles, FLoreSourceControlProvider& InProvider, bool bQueryLocks, TArray<FString>& OutErrorMessages, TArray<FLoreSourceControlState>& OutStates);

	/**
	 * Parser for lore status --json output. Populates per-file states and, if OutSummary is given,
	 * the repository-level facts (branch name, dirty/behind-remote flags) from the same single pass.
	 */
	LORESOURCECONTROL_API void ParseStatusResults(const FString& InResults, const TArray<FString>& InFiles, const FString& InRepositoryRoot, TArray<FLoreSourceControlState>& OutStates, FLoreStatusSummary* OutSummary = nullptr);

	/** Parse structured error events emitted by a Lore command. */
	LORESOURCECONTROL_API void ParseCommandErrors(const TArray<FString>& InResults, TArray<FString>& OutErrorMessages);

	/** True if the message is Lore reporting that the lock service is out of reach rather than a real fault. */
	LORESOURCECONTROL_API bool IsLockServiceUnavailableError(const FString& InError);

	/**
	 * Drop the lock-service errors that carry no information for the user:
	 * the optional owner-name lookup that Lore attempts after a successful query, and,
	 * when the service is unreachable, the connection failure itself.
	 */
	LORESOURCECONTROL_API void RemoveOptionalLockQueryErrors(bool bLockQuerySucceeded, TArray<FString>& InOutErrorMessages);

	/** Parse Lore file-history events and their following metadata events. */
	LORESOURCECONTROL_API void ParseHistoryResults(const TArray<FString>& InResults, const FString& InLoreBinary, const FString& InRepositoryRoot, const FString& InFile, FLoreSourceControlHistory& OutHistory);

	/** Parse and deduplicate Lore branch-list entries. */
	LORESOURCECONTROL_API void ParseBranchResults(const TArray<FString>& InResults, TArray<FLoreBranchInfo>& OutBranches);

	/** Parse lock-query results and optional owner display names. */
	LORESOURCECONTROL_API void ParseLockResults(const TArray<FString>& InResults, const FString& InRepositoryRoot, TMap<FString, FLoreLockOwner>& OutLockedBy);

	/**
	 * Run `lore file history <path>` and parse it into revision history entries, combining each
	 * "fileHistory" event with the "metadata" events (message/created-by/timestamp) that follow it -
	 * Lore reports commit message/author/date as separate metadata events, not on the history entry itself.
	 */
	bool RunGetHistory(const FString& InLoreBinary, const FString& InRepositoryRoot, const FString& InFile, TArray<FString>& OutErrorMessages, FLoreSourceControlHistory& OutHistory);

	/**
	 * Run `lore branch list` (local branches only) and parse the resulting "branchListEntry" events.
	 */
	bool RunGetBranches(const FString& InLoreBinary, const FString& InRepositoryRoot, TArray<FLoreBranchInfo>& OutBranches, TArray<FString>* OutErrorMessages = nullptr);

	/**
	 * Run `lore branch switch <name>` to switch the working copy to a different branch.
	 * Caller is responsible for warning the user beforehand - this changes files on disk.
	 */
	bool RunSwitchBranch(const FString& InLoreBinary, const FString& InRepositoryRoot, const FString& InBranchName, TArray<FString>& OutErrorMessages, TArray<FString>* OutChangedPaths = nullptr);

	/**
	 * Run `lore branch diff <target>` (current branch vs. target) and collect the repository-relative
	 * paths of every changed file. Used before a branch switch to decide whether Source/Config files
	 * are affected (requires an editor restart) or the change is Content-only (safe to hot-reload).
	 */
	bool RunGetBranchDiff(const FString& InLoreBinary, const FString& InRepositoryRoot, const FString& InTargetBranch, TArray<FString>& OutChangedPaths, TArray<FString>& OutErrorMessages);

	/**
	 * Splits a list of repository-relative changed paths into "touches Source/Config/.uplugin/.uproject"
	 * (returns true - needs an editor restart) vs plain Content paths (returned via OutContentPaths,
	 * safe to hot-reload). Shared by branch switch and sync, which both need this same classification.
	 */
	LORESOURCECONTROL_API bool ClassifyChangedPaths(const TArray<FString>& InPaths, TArray<FString>& OutContentPaths);

	/**
	 * Run `lore sync` and classify the "revisionSyncFile" events it reports the same way a branch
	 * switch is classified (see ClassifyChangedPaths) - so a Content-only sync can auto-reload
	 * instead of just telling the user to reload manually.
	 */
	bool RunSync(const FString& InLoreBinary, const FString& InRepositoryRoot, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages, TArray<FString>& OutChangedContentPaths, bool& OutRequiresRestart);

	/**
	 * Query every lock on the repository's current branch ("lock query --branch <name>").
	 * Unlike "lock status", this needs no file list - it lists every locked path in one call,
	 * which is what both a broad refresh and a single-file one actually need.
	 *
	 * Set bAllowBackoff for routine refreshes: once the service is known to be unreachable the query is skipped
	 * outright for a short cooldown instead of paying a process spawn and a connection timeout on every tick.
	 * Leave it off wherever a stale answer is unacceptable, such as the lock ownership check that guards a submit.
	 */
	ELoreLockQueryResult GetLoreLockStatus(const FString& InLoreBinary, const FString& InRepositoryRoot, const FLoreSourceControlProvider& InProvider, TMap<FString, FLoreLockOwner>& OutLockedBy, TArray<FString>* OutErrorMessages = nullptr, bool bAllowBackoff = false);

	/** Return every staged file and directory in the repository as normalized absolute paths. */
	LORESOURCECONTROL_API bool RunGetStagedPaths(const FString& InLoreBinary, const FString& InRepositoryRoot, TArray<FString>& OutStagedFiles, TArray<FString>& OutStagedDirectories, TArray<FString>& OutErrorMessages);

	/**
	 * Settings using UDeveloperSettings (Project Settings > Plugins > Lore Source Control).
	 * These replace the old manual ini file.
	 */
	void LoadSettings(FString& OutLoreBinaryPath);
	void SaveSettings(const FString& InLoreBinaryPath);

	/**
	 * Get the user-configured binary path from ULoreSourceControlSettings (may be empty).
	 */
	FString GetUserConfiguredLoreBinaryPath();

	/**
	 * Persist a binary path into the developer settings.
	 */
	void SetUserConfiguredLoreBinaryPath(const FString& InPath);

	/**
	 * Whether Check Out/Revert/Submit should actually acquire/release Lore locks (ULoreSourceControlSettings::bLockFiles).
	 */
	bool ShouldLockFiles();
	void SetShouldLockFiles(bool bShouldLockFiles);

	/**
	 * Update the provider's cached states from worker results.
	 */
	bool UpdateCachedStates(FLoreSourceControlProvider* InProvider, const TArray<FLoreSourceControlState>& InStates, const TArray<FString>& InScanPaths, bool bApplyResults);

	/**
	 * Retrieve the name of the currently active branch.
	 * Preferred: `lore branch info` ("branchInfo" event)
	 * Fallbacks: `lore status --revision-only` ("repositoryStatusRevision" event),
	 * then `lore branch list` (the entry with isCurrent set).
	 */
	FString GetCurrentBranchName(const FString& InLoreBinary, const FString& InRepositoryRoot);

} // namespace FLoreSourceControlUtils
