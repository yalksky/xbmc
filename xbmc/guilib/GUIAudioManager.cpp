/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "system.h"
#include "GUIAudioManager.h"
#include "Key.h"
#include "input/ButtonTranslator.h"
#include "settings/lib/Setting.h"
#include "threads/SingleLock.h"
#include "utils/URIUtils.h"
#include "utils/XBMCTinyXML.h"
#include "addons/Skin.h"
#include "cores/AudioEngine/AEFactory.h"

using namespace std;

CGUIAudioManager g_audioManager;

CGUIAudioManager::CGUIAudioManager()
{
  m_bEnabled = false;
}

CGUIAudioManager::~CGUIAudioManager()
{
}

void CGUIAudioManager::OnSettingChanged(const CSetting *setting)
{
  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();
  if (settingId == "lookandfeel.soundskin")
  {
    Enable(true);
    Load();
  }
}

void CGUIAudioManager::Initialize()
{
}

void CGUIAudioManager::DeInitialize()
{
  CSingleLock lock(m_cs);
  UnLoad();
}

void CGUIAudioManager::Stop()
{
  CSingleLock lock(m_cs);
  for (windowSoundMap::iterator it = m_windowSoundMap.begin(); it != m_windowSoundMap.end(); ++it)
  {
    if (it->second.initSound  ) it->second.initSound  ->Stop();
    if (it->second.deInitSound) it->second.deInitSound->Stop();
  }

  for (pythonSoundsMap::iterator it = m_pythonSounds.begin(); it != m_pythonSounds.end(); ++it)
  {
    IAESound* sound = it->second;
    sound->Stop();
  }
}

// \brief Play a sound associated with a CAction
void CGUIAudioManager::PlayActionSound(const CAction& action)
{
  CSingleLock lock(m_cs);

  // it's not possible to play gui sounds when passthrough is active
  if (!m_bEnabled)
    return;

  actionSoundMap::iterator it = m_actionSoundMap.find(action.GetID());
  if (it == m_actionSoundMap.end())
    return;

  if (it->second)
    it->second->Play();
}

// \brief Play a sound associated with a window and its event
// Events: SOUND_INIT, SOUND_DEINIT
void CGUIAudioManager::PlayWindowSound(int id, WINDOW_SOUND event)
{
  CSingleLock lock(m_cs);

  // it's not possible to play gui sounds when passthrough is active
  if (!m_bEnabled)
    return;

  windowSoundMap::iterator it=m_windowSoundMap.find(id);
  if (it==m_windowSoundMap.end())
    return;

  CWindowSounds sounds=it->second;
  IAESound *sound = NULL;
  switch (event)
  {
  case SOUND_INIT:
    sound = sounds.initSound;
    break;
  case SOUND_DEINIT:
    sound = sounds.deInitSound;
    break;
  }

  if (!sound)
    return;

  sound->Play();
}

// \brief Play a sound given by filename
void CGUIAudioManager::PlayPythonSound(const CStdString& strFileName)
{
  CSingleLock lock(m_cs);

  // it's not possible to play gui sounds when passthrough is active
  if (!m_bEnabled)
    return;

  // If we already loaded the sound, just play it
  pythonSoundsMap::iterator itsb=m_pythonSounds.find(strFileName);
  if (itsb != m_pythonSounds.end())
  {
    IAESound* sound = itsb->second;
    sound->Play();
    return;
  }

  IAESound *sound = LoadSound(strFileName);
  if (!sound)
    return;

  m_pythonSounds.insert(pair<const CStdString, IAESound*>(strFileName, sound));
  sound->Play();
}

void CGUIAudioManager::UnLoad()
{
  //  Free sounds from windows
  {
    windowSoundMap::iterator it = m_windowSoundMap.begin();
    while (it != m_windowSoundMap.end())
    {
      if (it->second.initSound  ) FreeSound(it->second.initSound  );
      if (it->second.deInitSound) FreeSound(it->second.deInitSound);
      m_windowSoundMap.erase(it++);
    }
  }

  // Free sounds from python
  {
    pythonSoundsMap::iterator it = m_pythonSounds.begin();
    while (it != m_pythonSounds.end())
    {
      IAESound* sound = it->second;
      FreeSound(sound);
      m_pythonSounds.erase(it++);
    }
  }

  // free action sounds
  {
    actionSoundMap::iterator it = m_actionSoundMap.begin();
    while (it != m_actionSoundMap.end())
    {
      IAESound* sound = it->second;
      FreeSound(sound);
      m_actionSoundMap.erase(it++);
    }
  }
}

// \brief Load the config file (sounds.xml) for nav sounds
// Can be located in a folder "sounds" in the skin or from a
// subfolder of the folder "sounds" in the root directory of
// xbmc
bool CGUIAudioManager::Load()
{
  CSingleLock lock(m_cs);

  UnLoad();

  if (CSettings::Get().GetString("lookandfeel.soundskin")=="OFF")
    return true;
  else
    Enable(true);

  if (CSettings::Get().GetString("lookandfeel.soundskin")=="SKINDEFAULT")
  {
    m_strMediaDir = URIUtils::AddFileToFolder(g_SkinInfo->Path(), "sounds");
  }
  else
    m_strMediaDir = URIUtils::AddFileToFolder("special://xbmc/sounds", CSettings::Get().GetString("lookandfeel.soundskin"));

  CStdString strSoundsXml = URIUtils::AddFileToFolder(m_strMediaDir, "sounds.xml");

  //  Load our xml file
  CXBMCTinyXML xmlDoc;

  CLog::Log(LOGINFO, "Loading %s", strSoundsXml.c_str());

  //  Load the config file
  if (!xmlDoc.LoadFile(strSoundsXml))
  {
    CLog::Log(LOGNOTICE, "%s, Line %d\n%s", strSoundsXml.c_str(), xmlDoc.ErrorRow(), xmlDoc.ErrorDesc());
    return false;
  }

  TiXmlElement* pRoot = xmlDoc.RootElement();
  CStdString strValue = pRoot->Value();
  if ( strValue != "sounds")
  {
    CLog::Log(LOGNOTICE, "%s Doesn't contain <sounds>", strSoundsXml.c_str());
    return false;
  }

  //  Load sounds for actions
  TiXmlElement* pActions = pRoot->FirstChildElement("actions");
  if (pActions)
  {
    TiXmlNode* pAction = pActions->FirstChild("action");

    while (pAction)
    {
      TiXmlNode* pIdNode = pAction->FirstChild("name");
      int id = 0;    // action identity
      if (pIdNode && pIdNode->FirstChild())
      {
        CButtonTranslator::TranslateActionString(pIdNode->FirstChild()->Value(), id);
      }

      TiXmlNode* pFileNode = pAction->FirstChild("file");
      CStdString strFile;
      if (pFileNode && pFileNode->FirstChild())
        strFile += pFileNode->FirstChild()->Value();

      if (id > 0 && !strFile.empty())
      {
        CStdString filename = URIUtils::AddFileToFolder(m_strMediaDir, strFile);
        IAESound *sound = LoadSound(filename);
        if (sound)
          m_actionSoundMap.insert(pair<int, IAESound *>(id, sound));
      }

      pAction = pAction->NextSibling();
    }
  }

  //  Load window specific sounds
  TiXmlElement* pWindows = pRoot->FirstChildElement("windows");
  if (pWindows)
  {
    TiXmlNode* pWindow = pWindows->FirstChild("window");

    while (pWindow)
    {
      int id = 0;

      TiXmlNode* pIdNode = pWindow->FirstChild("name");
      if (pIdNode)
      {
        if (pIdNode->FirstChild())
          id = CButtonTranslator::TranslateWindow(pIdNode->FirstChild()->Value());
      }

      CWindowSounds sounds;
      sounds.initSound   = LoadWindowSound(pWindow, "activate"  );
      sounds.deInitSound = LoadWindowSound(pWindow, "deactivate");

      if (id > 0)
        m_windowSoundMap.insert(pair<int, CWindowSounds>(id, sounds));

      pWindow = pWindow->NextSibling();
    }
  }

  return true;
}

IAESound* CGUIAudioManager::LoadSound(const CStdString &filename)
{
  CSingleLock lock(m_cs);
  soundCache::iterator it = m_soundCache.find(filename);
  if (it != m_soundCache.end())
  {
    ++it->second.usage;
    return it->second.sound;
  }

  IAESound *sound = CAEFactory::MakeSound(filename);
  if (!sound)
    return NULL;

  CSoundInfo info;
  info.usage = 1;
  info.sound = sound;
  m_soundCache[filename] = info;

  return info.sound;
}

void CGUIAudioManager::FreeSound(IAESound *sound)
{
  CSingleLock lock(m_cs);
  for(soundCache::iterator it = m_soundCache.begin(); it != m_soundCache.end(); ++it) {
    if (it->second.sound == sound) {
      if (--it->second.usage == 0) {     
        CAEFactory::FreeSound(sound);
        m_soundCache.erase(it);
      }
      return;
    }
  }
}

// \brief Load a window node of the config file (sounds.xml)
IAESound* CGUIAudioManager::LoadWindowSound(TiXmlNode* pWindowNode, const CStdString& strIdentifier)
{
  if (!pWindowNode)
    return NULL;

  TiXmlNode* pFileNode = pWindowNode->FirstChild(strIdentifier);
  if (pFileNode && pFileNode->FirstChild())
    return LoadSound(URIUtils::AddFileToFolder(m_strMediaDir, pFileNode->FirstChild()->Value()));

  return NULL;
}

// \brief Enable/Disable nav sounds
void CGUIAudioManager::Enable(bool bEnable)
{
  // always deinit audio when we don't want gui sounds
  if (CSettings::Get().GetString("lookandfeel.soundskin")=="OFF")
    bEnable = false;

  CSingleLock lock(m_cs);
  m_bEnabled = bEnable;
}

// \brief Sets the volume of all playing sounds
void CGUIAudioManager::SetVolume(float level)
{
  CSingleLock lock(m_cs);

  {
    actionSoundMap::iterator it = m_actionSoundMap.begin();
    while (it!=m_actionSoundMap.end())
    {
      if (it->second)
        it->second->SetVolume(level);
      ++it;
    }
  }

  for(windowSoundMap::iterator it = m_windowSoundMap.begin(); it != m_windowSoundMap.end(); ++it)
  {
    if (it->second.initSound  ) it->second.initSound  ->SetVolume(level);
    if (it->second.deInitSound) it->second.deInitSound->SetVolume(level);
  }

  {
    pythonSoundsMap::iterator it = m_pythonSounds.begin();
    while (it != m_pythonSounds.end())
    {
      if (it->second)
        it->second->SetVolume(level);

      ++it;
    }
  }
}
