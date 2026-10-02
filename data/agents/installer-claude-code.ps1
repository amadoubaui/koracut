# SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installe Claude Code sous Windows par la voie officielle d'Anthropic, après accord de la personne.
# Lancé par KoraCut dans son terminal quand « claude » est absent (document 18, 1er octobre 2026).
# Voie vérifiée le 1er octobre 2026 sur https://code.claude.com/docs/en/setup :
#   installeur natif (recommandé) : irm https://claude.ai/install.ps1 | iex
#   sinon, avec npm :               npm install -g @anthropic-ai/claude-code
# Le script ne lit, n'écrit et ne transmet aucun identifiant.
# Codes de retour : 0 installé (ou déjà là), 1 échec, 2 refusé.
# Écrit d'après la documentation, pas encore essayé sur un vrai PC.

$ErrorActionPreference = 'Stop'
$fr = ("$env:KORACUT_LANGUE" -like 'fr*') -or ((-not $env:KORACUT_LANGUE) -and ((Get-Culture).Name -like 'fr*'))
function Dire([string]$couleur, [string]$francais, [string]$anglais) {
    if ($fr) { Write-Host $francais -ForegroundColor $couleur } else { Write-Host $anglais -ForegroundColor $couleur }
}

$env:Path = "$env:USERPROFILE\.local\bin;$env:Path"
$present = Get-Command claude -ErrorAction SilentlyContinue
if ($present -and $env:KORACUT_CLAUDE_ABSENT -ne '1') {
    Dire Green "Claude Code est déjà installé : $($present.Source)" "Claude Code is already installed: $($present.Source)"
    exit 0
}

$commande = 'irm https://claude.ai/install.ps1 | iex'
Dire White "Claude Code n'est pas encore installé sur cet ordinateur." "Claude Code is not installed on this computer yet."
Dire Gray "KoraCut peut l'installer avec la commande officielle d'Anthropic :" "KoraCut can install it with Anthropic's official command:"
Write-Host ""
Write-Host "    $commande" -ForegroundColor Cyan
Write-Host ""
Dire DarkGray "Il sera posé dans $env:USERPROFILE\.local\bin et se mettra à jour seul. Pas besoin d'être administrateur." "It goes to $env:USERPROFILE\.local\bin and updates itself. No administrator rights needed."
Dire DarkGray "Cela télécharge le programme depuis claude.ai. Rien d'autre n'est modifié, KoraCut ne voit aucun identifiant." "This downloads the program from claude.ai. Nothing else is changed, KoraCut sees no credential."
Dire DarkGray "Il faut un abonnement Claude (Pro, Max, Team, Enterprise) ou un compte Console." "A Claude subscription (Pro, Max, Team, Enterprise) or a Console account is needed."
Write-Host ""
if ($fr) { $question = 'Installer maintenant ? [o/N]' } else { $question = 'Install now? [y/N]' }
$reponse = Read-Host $question
if ($reponse -notmatch '^(o|oui|y|yes)$') {
    Dire Yellow "Installation annulée. Rien n'a été téléchargé ni installé." "Installation cancelled. Nothing was downloaded or installed."
    exit 2
}
if ($env:KORACUT_INSTALLER_SIMULER -eq '1') {
    Dire Yellow "Essai : la commande n'est pas lancée." "Trial: the command is not run."
    exit 0
}

Dire White "Installation en cours…" "Installing…"
try {
    Invoke-RestMethod https://claude.ai/install.ps1 | Invoke-Expression
} catch {
    Dire Red "L'installeur officiel a échoué : $($_.Exception.Message)" "The official installer failed: $($_.Exception.Message)"
    if (Get-Command npm -ErrorAction SilentlyContinue) {
        Dire Gray "Deuxième voie officielle, par npm : npm install -g @anthropic-ai/claude-code" "Second official way, with npm: npm install -g @anthropic-ai/claude-code"
        npm install -g '@anthropic-ai/claude-code'
        if ($LASTEXITCODE -ne 0) { exit 1 }
        $env:Path = "$env:APPDATA\npm;$env:Path"
    } else {
        exit 1
    }
}
if (-not (Get-Command claude -ErrorAction SilentlyContinue)) {
    Dire Red "L'installation s'est terminée mais la commande claude reste introuvable. Ferme cet onglet et réessaie." "The installation ended but the claude command is still not found. Close this tab and try again."
    exit 1
}
Dire Green "Claude Code est installé : $(claude --version)" "Claude Code is installed: $(claude --version)"
Dire White "La première fois, Claude Code ouvre ton navigateur pour te connecter à ton compte Claude. C'est toi qui te connectes, KoraCut ne garde rien." "The first time, Claude Code opens your browser to sign in to your Claude account. You sign in yourself, KoraCut keeps nothing."
exit 0
