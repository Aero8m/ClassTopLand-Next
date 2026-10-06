param([Parameter(Mandatory = $true)][string]$AppFile)
$ErrorActionPreference = 'Stop'
$applicationPath = [System.IO.Path]::GetFullPath($AppFile)
if (-not [System.IO.File]::Exists($applicationPath)) { throw 'Expected a generated executable, not a directory.' }

& icacls $applicationPath /setintegritylevel M /Q
if ($LASTEXITCODE -eq 0) { exit 0 }

# Some project drives grant Modify rather than FullControl. Changing a label
# needs WRITE_OWNER, which Modify omits. The owner can edit the DACL: grant only
# that owner WRITE_OWNER for this operation, then restore the original DACL.
# Never grant other users access, modify a directory, or request administrator
# elevation merely to normalize our own generated executable.
$applicationAcl = [System.IO.File]::GetAccessControl($applicationPath)
$currentSid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
$ownerSid = $applicationAcl.GetOwner([System.Security.Principal.SecurityIdentifier])
if ($ownerSid.Value -ne $currentSid.Value) {
    throw 'The generated executable belongs to another user; normalize it from its owner account.'
}
$accessSection = [System.Security.AccessControl.AccessControlSections]::Access
$originalDacl = $applicationAcl.GetSecurityDescriptorSddlForm($accessSection)
$temporaryRule = [System.Security.AccessControl.FileSystemAccessRule]::new(
    $currentSid, [System.Security.AccessControl.FileSystemRights]::TakeOwnership,
    [System.Security.AccessControl.AccessControlType]::Allow)
try {
    $applicationAcl.AddAccessRule($temporaryRule)
    [System.IO.File]::SetAccessControl($applicationPath, $applicationAcl)
    & icacls $applicationPath /setintegritylevel M /Q
    if ($LASTEXITCODE -ne 0) { throw 'Could not set the executable integrity label to Medium.' }
}
finally {
    # Restore only the DACL; retain the new integrity label and original owner.
    $restoredAcl = [System.IO.File]::GetAccessControl($applicationPath)
    $restoredAcl.SetSecurityDescriptorSddlForm($originalDacl, $accessSection)
    [System.IO.File]::SetAccessControl($applicationPath, $restoredAcl)
}
