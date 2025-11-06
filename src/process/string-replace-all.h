#pragma once
#include <string>

template <class CharT>
inline void string_replace_all(std::basic_string<CharT>& s, const std::basic_string<CharT>& from, const std::basic_string<CharT>& to) {
	if (from.empty())
		return;
	size_t start_pos = 0;
	while ((start_pos = s.find(from, start_pos)) != std::basic_string<CharT>::npos) {
		s.replace(start_pos, from.length(), to);
		start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
	}
}
