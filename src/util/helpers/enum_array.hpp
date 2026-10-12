#pragma once

template<typename E, class T>
class enum_array : public std::array<T, static_cast<size_t>(E::ENUM_COUNT)> {
public:
	T& operator[] (E e) {
		return std::array<T, static_cast<size_t>(E::ENUM_COUNT)>::operator[]((std::size_t)e);
	}

	const T& operator[] (E e) const {
		return std::array<T, static_cast<size_t>(E::ENUM_COUNT)>::operator[]((std::size_t)e);
	}
};