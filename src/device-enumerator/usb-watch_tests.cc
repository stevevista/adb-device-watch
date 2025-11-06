
TEST(UsbWatch, TestAppendCharToWchar) {
  wchar_t wfriendlyname[18] = L"Abcd efgh";
  std::string identity = "COM12";
  size_t len = std::wcslen(wfriendlyname);
  size_t lefts = sizeof(wfriendlyname)/2 - len;
  if (lefts > identity.size() + 3) {
        wfriendlyname[len++] = L' ';
        wfriendlyname[len++] = L'(';
        DWORD n = ::MultiByteToWideChar(CP_ACP, 0, identity.c_str(), (int)identity.size(), &wfriendlyname[len], (int)(lefts - 2));
        len += n;
        wfriendlyname[len++] = L')';
        wfriendlyname[len++] = 0;
  }

  // wprintf(L"%s\n", wfriendlyname);
  ASSERT_TRUE(std::wcscmp(wfriendlyname, L"Abcd efgh (COM12)") == 0);
}

TEST(UsbWatch, TransformDevpathToDevId) {
  auto devpath = "\\\\?\\usb#vid_31ef&pid_9091&mi_03#6&897122b&0&0003#{f72fe0d4-cbcb-407d-8814-9ed673d0dd6b}";
  auto devid = TransformDevpathToDevId(devpath);
  ASSERT_EQ(devid, "USB\\VID_31EF&PID_9091&MI_03\\6&897122B&0&0003");
}
