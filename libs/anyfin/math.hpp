
#pragma once

#include "anyfin/base.hpp"

#include "anyfin/prelude.hpp"

#include <intrin.h>

namespace Fin {

template<typename T>
constexpr T square (T a) {
  const T result = a * a;
  return result;
}

template<typename T>
constexpr T cube (T a) {
  const T result = a * a * a;
  return result;
}

static inline u32 round_up_to_u32 (const f32 value) {
  return (u32) _mm_cvtss_si32(_mm_ceil_ss(_mm_setzero_ps(), _mm_set_ss(value)));
}

static inline u32 round_down_to_u32 (const f32 value) {
  return (u32) _mm_cvtss_si32(_mm_floor_ss(_mm_setzero_ps(), _mm_set_ss(value)));
}

static inline s32 round_up_to_s32 (const f32 value) {
  return _mm_cvtss_si32(_mm_ceil_ss(_mm_setzero_ps(), _mm_set_ss(value)));
}

static inline s32 round_down_to_s32 (const f32 value) {
  return _mm_cvtss_si32(_mm_floor_ss(_mm_setzero_ps(), _mm_set_ss(value)));
}

static inline u32 round_up_to_pow_2 (u32 value) {
  if (is_power_of_2(value)) return value;
  if (value == 0)           return value;

  value -= 1;
  for (u32 index = 0; index < (sizeof(value) * 8); index *= 2) {
    value |= value >> index;
  }

  return value + 1;
}

/* static inline float square_root (const float value) { */
/*   const float result = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(value))); */
/*   return result; */
/* } */

/* static inline float cosine (const float rad_value) { */
/*   const float result = cosf(rad_value); */
/*   return result; */
/* } */

// static inline float fractional_part (float value) {
//   if (value > 0.0f) return value - (int32_t) value;
//   else return value - ((int32_t) value + 1);
// }

template<typename T>
union Vec2 {
  struct { T x, y; };
  struct { T width, height; };
  T elems[2];
};

template<typename T>
constexpr Vec2<T> vec2 (T x, T y) {
  return { .x = x, .y = y};
}

template<typename T>
constexpr Vec2<T> vec2 (T value) {
  return { .x = value, .y = value };
}

template <typename T>
constexpr T length (const Vec2<T> &vec) {
  // TODO: Is this leagal?
  const T result = static_cast<T>(sqrt(vec.x * vec.x + vec.y * vec.y));
  return result;
}

template<typename T>
constexpr Vec2<T> operator + (const Vec2<T> left, const Vec2<T> other) {
  const Vec2<T> result = { left.x + other.x, left.y + other.y };
  return result;
}

template<typename T>
constexpr Vec2<T> operator + (const Vec2<T> left, const T value) {
  const Vec2<T> result = { left.x + value, left.y + value };
  return result;
}

template<typename T>
constexpr Vec2<T>& operator += (Vec2<T> &left, const T value) {
  left.x += value;
  left.y += value;
  return left;
}

template<typename T>
constexpr Vec2<T> operator += (Vec2<T> &left, const Vec2<T> other) {
  left.x += other.x;
  left.y += other.y;
  return left;
}

template<typename T>
constexpr Vec2<T> operator - (const Vec2<T> left, const Vec2<T> other) {
  const Vec2<T> result = { left.x - other.x, left.y - other.y };
  return result;
}

template<typename T>
constexpr Vec2<T> operator * (const Vec2<T> left, const T value) {
  const Vec2<T> result = { left.x * value, left.y * value };
  return result;
}

template<typename T>
constexpr Vec2<T> operator / (const Vec2<T> left, const T value) {
  fin_ensure(static_cast<usize>(value) != 0);
  const Vec2<T> result = { left.x / value, left.y / value };
  return result;
}

template <typename T>
constexpr bool operator < (const Vec2<T> &left, const Vec2<T> &right) {
  const bool result = length(left) < length(right);
  return result;
}

template <typename T>
constexpr Vec2<f32> lerp (const Vec2<T> &a, const Vec2<f32> &b, const f32 step) {
  const Vec2<T> result = a * (1.0f - step) + b * step;
  return result;
}

template<typename T>
union Vec3 {
  struct { T x, y, z; };
  struct { T u, v, w; };
  struct { T r, g, b; };
  T elems[3];
};

// static inline float dot (const union Vec3 left, const union Vec3 right) {
//   const float result = left.x * right.x + left.y * right.y + left.z * right.z;
//   return result;
// }

/* static inline float length (const Vec3& vector) { */
/*   float result = square_root(dot(vector, vector)); */
/*   return result; */
/* } */

/* inline Vec3 operator - (const Vec3& left, const Vec3& right) { */
/*   Vec3 result = {}; */

/*   result.x = left.x - right.x; */
/*   result.y = left.y - right.y; */
/*   result.z = left.z - right.z; */

/*   return result; */
/* } */

/* inline Vec3 operator - (const Vec3& left, float scalar) { */
/*   Vec3 result = {}; */

/*   result.x = left.x - scalar; */
/*   result.y = left.y - scalar; */
/*   result.z = left.z - scalar; */

/*   return result; */
/* } */

/* inline Vec3& operator += (Vec3& left, Vec3& right) { */
/*   left.x += right.x; */
/*   left.y += right.y; */
/*   left.z += right.z; */

/*   return left; */
/* } */

/* inline Vec3 operator + (const Vec3& left, const Vec3& right) { */
/*   Vec3 result = {}; */

/*   result.x = left.x + right.x; */
/*   result.y = left.y + right.y; */
/*   result.z = left.z + right.z; */

/*   return result; */
/* } */

/* inline Vec3 operator * (const Vec3& vector, float value) { */
/*   Vec3 result = {}; */
  
/*   result.x = vector.x * value; */
/*   result.y = vector.y * value; */
/*   result.z = vector.z * value; */
  
/*   return result; */
/* } */

/* inline Vec3& operator *= (Vec3& vector, float value) { */
/*   vector.x = vector.x * value; */
/*   vector.y = vector.y * value; */
/*   vector.z = vector.z * value; */
  
/*   return vector; */
/* } */

/* inline Vec3 operator * (float value, const Vec3& vector) { */
/*   Vec3 result = vector * value; */
/*   return result; */
/* } */

/* inline Vec3 operator - (const Vec3& vector) { */
/*   Vec3 result = vector * -1.0f; */
/*   return result; */
/* } */

/* inline Vec3 operator / (const Vec3& vector, float value) { */
/*   Vec3 result = vector * (1.0f / value); */
/*   return result; */
/* } */

/* inline Vec3& operator /= (Vec3& vector, float value) { */
/*   float recip = 1.0f / value; */

/*   vector.x = vector.x * recip; */
/*   vector.y = vector.y * recip; */
/*   vector.z = vector.z * recip; */
  
/*   return vector; */
/* } */

/* inline Vec3 normalise (const Vec3 & vector) { */
/*   float len = length(vector); */
/*   fin_ensure(len != 0.0f); */
    
/*   Vec3 normalized = vector * (1.0f / len); */
  
/*   return normalized; */
/* } */

/* inline Vec3 cross (const Vec3& left, const Vec3& right) { */
/*   Vec3 result = {}; */

/*   result.x = left.y * right.z - left.z * right.y; */
/*   result.y = left.z * right.x - left.x * right.z; */
/*   result.z = left.x * right.y - left.y * right.x; */

/*   return result; */
/* } */

template<typename T>
union Vec4 {
  struct { T x, y, z, w; };
  struct { Vec2<T> p1, p2; };
  struct { T r, g, b, a; };
  struct { Vec3<T> xyz; T __IGNORED_0__; };
  T elems[4];

  constexpr Vec4 () = default;
  constexpr Vec4 (const T array[4])
    : elems { array[0], array[1], array[2], array[3] } 
  {}

  constexpr Vec4 (const T x, const T y, const T z, const T w)
    : elems { x, y, z, w }
  {}
};

template <typename T>
constexpr Vec4<T> vec4 (const T x, const T y, const T z, const T w) {
  Vec4<T> result {x, y, z, w};
  return result;
}

/* inline Vec4_t Vec4 (const Vec3& Vec3, float w_value = 1.0f) { */
/*   Vec4_t result = {}; */

/*   result.xyz = Vec3; */
/*   result.w   = w_value; */

/*   return result; */
/* } */

/* inline Vec4_t operator + (const Vec4_t& left, const Vec4_t& right) { */
/*   Vec4_t result = {}; */

/*   result.x = left.x + right.x; */
/*   result.y = left.y + right.y; */
/*   result.z = left.z + right.z; */
/*   result.w = left.w + right.w; */

/*   return result; */
/* } */

/* inline Vec4_t operator - (const Vec4_t& left, const Vec4_t& right) { */
/*   Vec4_t result = {}; */

/*   result.x = left.x - right.x; */
/*   result.y = left.y - right.y; */
/*   result.z = left.z - right.z; */


/*   return result; */
/* } */

/* inline Vec4_t operator - (const Vec4_t& left, float value) { */
/*   Vec4_t result = {}; */

/*   result.x = left.x - value; */
/*   result.y = left.y - value; */
/*   result.z = left.z - value; */

/*   return result; */
/* } */

/* inline Vec4_t operator * (const Vec4_t& left, float scalar) { */
/*   Vec4_t scaled = { left.x * scalar, left.y * scalar, left.z * scalar, left.w * scalar }; */
/*   return scaled; */
/* } */

/* inline Vec4_t operator - (const Vec4_t& vector) { */
/*   Vec4_t result = vector * -1.0f; */
/*   return result; */
/* } */

/* inline Vec4_t& operator /= (Vec4_t& vector, float value) { */
/*   float recip = 1.0f / value; */

/*   vector.x = vector.x * recip; */
/*   vector.y = vector.y * recip; */
/*   vector.z = vector.z * recip; */
/*   vector.w = vector.w * recip; */
  
/*   return vector; */
/* } */

/* static inline union Vec3 Vec3_make (const float a, const float b, const float c) { */
/*   const Vec3 result = { a, b, c }; */
/*   return result; */
/* } */

/* inline Vec3 Vec3 (Vec4_t in) { */
/*   if (in.w != 1.0f) { in /= in.w; } */
/*   return in.xyz; */
/* } */

/* inline float dot (const Vec4_t& left, const Vec4_t& right) { */
/*   float result = left.x * right.x + left.y * right.y + left.z * right.z + left.w * right.w; */
/*   return result; */
/* } */

/* inline float length (const Vec4_t& vector) { */
/*   float result = square_root(dot(vector, vector)); */
/*   return result; */
/* } */

/* inline Vec4_t normalise (const Vec4_t& vector) { */
/*   float len = length(vector); */
/*   fin_ensure(len != 0.0f); */
    
/*   Vec4_t normalized = vector * (1.0f / len); */
  
/*   return normalized; */
/* } */

/* inline Vec4_t cross (const Vec4_t& left, const Vec4_t& right) { */
/*   fin_ensure(left.w == 0.0f && right.w == 0.0f); */

/*   Vec4_t result = Vec4(cross(left.xyz, right.xyz), 0.0f); */

/*   return result; */
/* } */

/* /\** */
/*  * #NOTE(4lex1v, 01/06/19) */
/*  *   Using the column-major format here, hence axis are columns. */
/*  *\/ */
/* union mat3_t { */
/*   float elems[3 * 3]; // column major */
    
/*   struct { */
/*     Vec3 x_axis; */
/*     Vec3 y_axis; */
/*     Vec3 z_axis; */
/*   }; */
    
/*   struct { */
/*     Vec3 column0; */
/*     Vec3 column1; */
/*     Vec3 column2; */
/*   }; */

/*   inline float& operator () (uint32_t col, uint32_t row) { */
/*     const uint32_t offset = col * 3 + row; */
/*     fin_ensure(offset < array_size(elems)); */
    
/*     return elems[offset]; */
/*   } */
  
/*   inline float const& operator () (uint32_t col, uint32_t row) const { */
/*     const uint32_t offset = col * 3 + row; */
/*     fin_ensure(offset < array_size(elems)); */
    
/*     return elems[offset]; */
/*   } */

/* }; */

/* inline Vec3 get_row (const mat3_t& matrix, uint32_t row_index) { */
/*   Vec3 result = {}; */
  
/*   result(0) = matrix.column0(row_index); */
/*   result(1) = matrix.column1(row_index); */
/*   result(2) = matrix.column2(row_index); */
  
/*   return result; */
/* } */

/* inline void set_row (mat3_t& matrix, uint32_t row_index, Vec3& row) { */
/*   matrix(0, row_index) = row(0); */
/*   matrix(1, row_index) = row(1); */
/*   matrix(2, row_index) = row(2); */
/* } */

/* inline Vec3 get_column (const mat3_t& matrix, uint32_t col_index) { */
/*   Vec3 result = {}; */

/*   result(0) = matrix(col_index, 0); */
/*   result(1) = matrix(col_index, 1); */
/*   result(2) = matrix(col_index, 2); */
  
/*   return result; */
/* } */

/* inline void set_column (mat3_t& matrix, uint32_t idx, Vec3& values) { */
/*   matrix(idx, 0) = values(0); */
/*   matrix(idx, 1) = values(1); */
/*   matrix(idx, 2) = values(2); */
/* } */

/* inline Vec3 operator * (const mat3_t& matrix, const Vec3& vector) { */
/*   Vec3 result = {}; */

/*   auto row_0 = get_row(matrix, 0); */
/*   result(0) = dot(row_0, vector); */
  
/*   auto row_1 = get_row(matrix, 1); */
/*   result(1) = dot(row_1, vector); */
  
/*   auto row_2 = get_row(matrix, 2); */
/*   result(2) = dot(row_2, vector); */
  
/*   return result; */
/* } */

/* inline mat3_t operator * (const mat3_t& left, const mat3_t& right) { */
/*   mat3_t result = {}; */

/*   // Row 0 */
/*   Vec3 __left_mat_row_0 = get_row(left, 0); */
/*   Vec3 row_0 = {}; */
/*   row_0(0) = dot(__left_mat_row_0, right.column0); */
/*   row_0(1) = dot(__left_mat_row_0, right.column1); */
/*   row_0(2) = dot(__left_mat_row_0, right.column2); */
/*   set_row(result, 0, row_0); */
  
/*   // Row 1 */
/*   Vec3 __left_mat_row_1 = get_row(left, 1); */
/*   Vec3 row_1 = {}; */
/*   row_1(0) = dot(__left_mat_row_1, right.column0); */
/*   row_1(1) = dot(__left_mat_row_1, right.column1); */
/*   row_1(2) = dot(__left_mat_row_1, right.column2); */
/*   set_row(result, 1, row_1); */
  
/*   // Row 2 */
/*   Vec3 __left_mat_row_2 = get_row(left, 2); */
/*   Vec3 row_2 = {}; */
/*   row_2(0) = dot(__left_mat_row_2, right.column0); */
/*   row_2(1) = dot(__left_mat_row_2, right.column1); */
/*   row_2(2) = dot(__left_mat_row_2, right.column2); */
/*   set_row(result, 2, row_2); */
  
/*   return result; */
/* } */

/* inline mat3_t identity_mat3 () { */
/*   mat3_t result = {}; */
  
/*   result(0, 0) = 1.0f; */
/*   result(1, 1) = 1.0f; */
/*   result(2, 2) = 1.0f; */
  
/*   return result; */
/* } */

/* constexpr float epsi = static_cast<float>(1e-5); */

/* inline mat3_t mat3_x_rotation_rad (float angle_rad) { */
/*   // float angle_rad = DEG2RAD(angle_deg); */

/*   float cos_angle = cos(angle_rad); */
/*   float sin_angle = sin(angle_rad); */
  
/*   mat3_t result = identity_mat3(); */

/*   result.y_axis.y = cos_angle; */
/*   result.y_axis.z = sin_angle; */

/*   result.z_axis.y = -sin_angle; */
/*   result.z_axis.z = cos_angle; */

/*   return result; */
/* } */

/* inline mat3_t mat3_y_rotation_rad (float angle_rad) { */
/*   // float angle_rad = DEG2RAD(angle_deg); */
  
/*   float cos_angle = cos(angle_rad); */
/*   float sin_angle = sin(angle_rad); */

/*   mat3_t result = identity_mat3(); */

/*   result.x_axis.x = cos_angle; */
/*   result.x_axis.z = -sin_angle; */
  
/*   result.z_axis.x = sin_angle; */
/*   result.z_axis.z = cos_angle; */

/*   return result; */
/* } */

/* inline mat3_t mat3_z_rotation_rad (float angle_rad) { */
/*   // float angle_rad = DEG2RAD(angle_deg); */

/*   float cos_angle = cos(angle_rad); */
/*   float sin_angle = sin(angle_rad); */
  
/*   mat3_t result = identity_mat3(); */

/*   result.x_axis.x = cos_angle; */
/*   result.x_axis.y = sin_angle; */
  
/*   result.y_axis.x = -sin_angle; */
/*   result.y_axis.y = cos_angle; */

/*   return result; */
/* } */

/* inline mat3_t mat3_from_rows (const Vec3& row1, const Vec3& row2, const Vec3& row3) { */
/*   mat3_t result = {}; */

/*   result(0, 0) = row1(0); */
/*   result(1, 0) = row1(1); */
/*   result(2, 0) = row1(2); */
  
/*   result(0, 1) = row2(0); */
/*   result(1, 1) = row2(1); */
/*   result(2, 1) = row2(2); */
  
/*   result(0, 2) = row3(0); */
/*   result(1, 2) = row3(1); */
/*   result(2, 2) = row3(2); */
  
/*   return result; */
/* } */

/* inline mat3_t mat3_from_cols (const Vec3& col0, const Vec3& col1, const Vec3& col2) { */
/*   mat3_t result = identity_mat3(); */
  
/*   result.column0 = col0; */
/*   result.column1 = col1; */
/*   result.column2 = col2; */
  
/*   // result(0, 0) = col1(0); */
/*   // result(0, 1) = col1(1); */
/*   // result(0, 2) = col1(2); */
  
/*   // result(1, 0) = col2(0); */
/*   // result(1, 1) = col2(1); */
/*   // result(1, 2) = col2(2); */
  
/*   // result(2, 0) = col3(0); */
/*   // result(2, 1) = col3(1); */
/*   // result(2, 2) = col3(2); */
  
/*   return result; */
/* } */

/* // inline float determinant (const mat3_t& mat) {} */

/* // inline mat3_t inverse (const mat3_t& mat) { */
  
/* //   return {}; */
/* // } */

/* /\** */
/*  * #TODO(4lex1v, 01/29/19, SPEED) :: inline implementation */
/*  *\/ */
/* inline mat3_t transpose (const mat3_t& original) { */
/*   mat3_t result = original; */

/*   result(0, 1) = original(1, 0); */
/*   result(1, 0) = original(0, 1); */
  
/*   result(2, 0) = original(0, 2); */
/*   result(0, 2) = original(2, 0); */
  
/*   result(2, 1) = original(1, 2); */
/*   result(1, 2) = original(2, 1); */
  
/*   return result; */
/* } */
/* /\** */
/*  * #NOTE(4lex1v, 01/06/19) */
/*  *   Using the column-major format here, hence axis are columns. */
/*  * Size  */
/*  *\/ */
/* union mat4_t { */
/*   float elems[4 * 4]; // column major */
    
/*   struct { */
/*     Vec4_t x_axis; */
/*     Vec4_t y_axis; */
/*     Vec4_t z_axis; */
/*     Vec4_t translation; */
/*   }; */
    
/*   struct { */
/*     Vec4_t column0; */
/*     Vec4_t column1; */
/*     Vec4_t column2; */
/*     Vec4_t column3; */
/*   }; */

/*   inline float& operator () (const uint32_t col, const uint32_t row) { */
/*     const uint32_t offset = col * 4 + row; */
/*     fin_ensure(offset <= array_size(elems)); */
    
/*     return elems[offset]; */
/*   } */
  
/*   inline float const& operator () (const uint32_t col, const uint32_t row) const { */
/*     const uint32_t offset = col * 4 + row; */
/*     fin_ensure(offset <= array_size(elems)); */
    
/*     return elems[offset]; */
/*   } */
  
/* }; */

/* // CONSTRUCTORS // */

/* inline mat4_t identity_mat4 () { */
/*   mat4_t result = {}; */

/*   result.x_axis.x      = 1.0f; */
/*   result.y_axis.y      = 1.0f; */
/*   result.z_axis.z      = 1.0f; */
/*   result.translation.w = 1.0f; */
  
/*   return result; */
/* } */

/* // OPS // */

/* inline Vec4_t get_row (const mat4_t& matrix, uint32_t row_index) { */
/*   Vec4_t result = {}; */

/*   result(0) = matrix.column0(row_index); */
/*   result(1) = matrix.column1(row_index); */
/*   result(2) = matrix.column2(row_index); */
/*   result(3) = matrix.column3(row_index); */
  
/*   return result; */
/* } */

/* inline Vec4_t get_column (const mat4_t& matrix, uint32_t col_index) { */
/*   Vec4_t result = {}; */
  
/*   result = *(const Vec4_t*) (matrix.elems + col_index * 4); */
  
/*   // result(0) = matrix(col_index, 0); */
/*   // result(1) = matrix(col_index, 1); */
/*   // result(2) = matrix(col_index, 2); */
/*   // result(3) = matrix(col_index, 3); */
  
/*   return result; */
/* } */

/* inline void set_row (mat4_t& matrix, uint32_t row_index, const Vec4_t& row) { */
/*   matrix(0, row_index) = row(0); */
/*   matrix(1, row_index) = row(1); */
/*   matrix(2, row_index) = row(2); */
/*   matrix(3, row_index) = row(3); */
/* } */

/* inline void set_row (mat4_t& matrix, uint32_t row_index, const Vec3& row, float w_value) { */
/*   matrix(0, row_index) = row(0); */
/*   matrix(1, row_index) = row(1); */
/*   matrix(2, row_index) = row(2); */
/*   matrix(3, row_index) = w_value; */
/* } */

/* inline void set_column (mat4_t& matrix, uint32_t col_index, const Vec4_t& column) { */
/*   matrix(col_index, 0) = column(0); */
/*   matrix(col_index, 1) = column(1); */
/*   matrix(col_index, 2) = column(2); */
/*   matrix(col_index, 3) = column(3); */
/* } */

/* inline void set_column (mat4_t& matrix, uint32_t col_index, const Vec3& column, float w_value) { */
/*   matrix(col_index, 0) = column(0); */
/*   matrix(col_index, 1) = column(1); */
/*   matrix(col_index, 2) = column(2); */
/*   matrix(col_index, 3) = w_value; */
/* } */

/* inline Vec4_t operator * (const mat4_t& left, const Vec4_t& right) { */
/*   Vec4_t result = {}; */

/*   auto row_0 = get_row(left, 0); */
/*   result(0) = dot(row_0, right); */
  
/*   auto row_1 = get_row(left, 1); */
/*   result(1) = dot(row_1, right); */
  
/*   auto row_2 = get_row(left, 2); */
/*   result(2) = dot(row_2, right); */
  
/*   auto row_3 = get_row(left, 3); */
/*   result(3) = dot(row_3, right); */
  
/*   return result; */
/* } */

/* inline mat4_t operator * (const mat4_t& left, const mat4_t& right) { */
/*   mat4_t result = {}; */

/*   // Row 0 */
/*   Vec4_t __left_mat_row_0 = get_row(left, 0); */
/*   Vec4_t row_0 = {}; */
/*   row_0(0) = dot(__left_mat_row_0, right.column0); */
/*   row_0(1) = dot(__left_mat_row_0, right.column1); */
/*   row_0(2) = dot(__left_mat_row_0, right.column2); */
/*   row_0(3) = dot(__left_mat_row_0, right.column3); */
/*   set_row(result, 0, row_0); */
  
/*   // Row 1 */
/*   Vec4_t __left_mat_row_1 = get_row(left, 1); */
/*   Vec4_t row_1 = {}; */
/*   row_1(0) = dot(__left_mat_row_1, right.column0); */
/*   row_1(1) = dot(__left_mat_row_1, right.column1); */
/*   row_1(2) = dot(__left_mat_row_1, right.column2); */
/*   row_1(3) = dot(__left_mat_row_1, right.column3); */
/*   set_row(result, 1, row_1); */
  
/*   // Row 2 */
/*   Vec4_t __left_mat_row_2 = get_row(left, 2); */
/*   Vec4_t row_2 = {}; */
/*   row_2(0) = dot(__left_mat_row_2, right.column0); */
/*   row_2(1) = dot(__left_mat_row_2, right.column1); */
/*   row_2(2) = dot(__left_mat_row_2, right.column2); */
/*   row_2(3) = dot(__left_mat_row_2, right.column3); */
/*   set_row(result, 2, row_2); */
  
/*   // Row 3 */
/*   Vec4_t __left_mat_row_3 = get_row(left, 3); */
/*   Vec4_t row_3 = {}; */
/*   row_3(0) = dot(__left_mat_row_3, right.column0); */
/*   row_3(1) = dot(__left_mat_row_3, right.column1); */
/*   row_3(2) = dot(__left_mat_row_3, right.column2); */
/*   row_3(3) = dot(__left_mat_row_3, right.column3); */
/*   set_row(result, 3, row_3); */
  
/*   return result; */
/* } */

/* // Create's a column major matrix with the given columns */
/* inline mat4_t mat4_from_rows (const Vec3& side, const Vec3& up, const Vec3& forward, const Vec3& translation) { */
/*   mat4_t result = {}; */
  
/*   set_row(result, 0,     side,    0); */
/*   set_row(result, 1,      up,     0); */
/*   set_row(result, 2,   forward,   0); */
/*   set_row(result, 3, translation, 1); */

/*   return result; */
/* } */

/* inline mat4_t mat4_from_rows (const Vec4_t& side, const Vec4_t& up, const Vec4_t& forward, const Vec4_t& translation) { */
/*   mat4_t result = {}; */
  
/*   set_row(result, 0, side); */
/*   set_row(result, 1, up); */
/*   set_row(result, 2, forward); */
/*   set_row(result, 3, translation); */

/*   return result; */
/* } */

/* inline mat4_t mat4_from_cols (Vec3& x_axis, Vec3& y_axis, Vec3& z_axis, Vec3& translation) { */
/*   mat4_t result = identity_mat4(); */

/*   set_column(result, 0,      x_axis, 0.0f); */
/*   set_column(result, 1,      y_axis, 0.0f); */
/*   set_column(result, 2,      z_axis, 0.0f); */
/*   set_column(result, 3, translation, 1.0f); */
  
/*   return result; */
/* } */

/* // OPERATORS // */

/* inline mat4_t transpose (const mat4_t& original) { */
/*   mat4_t result = original; */

/*   result(0, 1) = original(1, 0); */
/*   result(1, 0) = original(0, 1); */
  
/*   result(2, 0) = original(0, 2); */
/*   result(0, 2) = original(2, 0); */
  
/*   result(2, 1) = original(1, 2); */
/*   result(1, 2) = original(2, 1); */
  
/*   result(0, 3) = original(3, 0); */
/*   result(3, 0) = original(0, 3); */

/*   result(1, 3) = original(3, 1); */
/*   result(3, 1) = original(1, 3); */
  
/*   result(2, 3) = original(3, 2); */
/*   result(3, 2) = original(2, 3); */
  
/*   return result; */
/* } */

/* /\** */
/*  * #NOTE(4lex1v, 01/22/19) :: Bottom row assumed to be [0 0 0 1] */
/*  *\/ */
/* inline mat4_t affine_inverse (const mat4_t& mat) { */
/*   mat4_t result = {}; */
    
/*   // compute upper left 3x3 matrix (rotation part) determinant */
/*   float cofactor0 = mat.elems[5] * mat.elems[10] - mat.elems[9] * mat.elems[6]; */
/*   float cofactor4 = mat.elems[1] * mat.elems[10] - mat.elems[9] * mat.elems[2]; */
/*   float cofactor8 = mat.elems[1] * mat.elems[6]  - mat.elems[5] * mat.elems[2]; */
    
/*   float det = mat.elems[0] * cofactor0 - mat.elems[4] * cofactor4 + mat.elems[8] * cofactor8; */
    
/*   if (fabs(det) <= epsi) { */
/*     fin_ensure(false); */
    
/*     // close enough to zero */
/*     // #TODO(4lex1v, 12/25/18) :: Need some error mechanics */
/*     return {}; */
/*   } */

/*   // create adjunct matrix and multiply by 1/det to get upper 3x3 */
/*   float invDet = 1.0f / det; */
/*   result.elems[0] = invDet * cofactor0; */
/*   result.elems[1] = invDet * cofactor4; */
/*   result.elems[2] = invDet * cofactor8; */
   
/*   result.elems[4] = invDet * (mat.elems[6] * mat.elems[8]  - mat.elems[4] * mat.elems[10]); */
/*   result.elems[5] = invDet * (mat.elems[0] * mat.elems[10] - mat.elems[2] * mat.elems[8]); */
/*   result.elems[6] = invDet * (mat.elems[2] * mat.elems[4]  - mat.elems[0] * mat.elems[6]); */

/*   result.elems[8]  = invDet * (mat.elems[4] * mat.elems[9] - mat.elems[5] * mat.elems[8]); */
/*   result.elems[9]  = invDet * (mat.elems[1] * mat.elems[8] - mat.elems[0] * mat.elems[9]); */
/*   result.elems[10] = invDet * (mat.elems[0] * mat.elems[5] - mat.elems[1] * mat.elems[4]); */

/*   // multiply -translation by inverted 3x3 to get its inverse */
    
/*   result.elems[12] = -result.elems[0] * mat.elems[12] - result.elems[4] * mat.elems[13] - result.elems[8] * mat.elems[14]; */
/*   result.elems[13] = -result.elems[1] * mat.elems[12] - result.elems[5] * mat.elems[13] - result.elems[9] * mat.elems[14]; */
/*   result.elems[14] = -result.elems[2] * mat.elems[12] - result.elems[6] * mat.elems[13] - result.elems[10] * mat.elems[14]; */

/*   result.elems[15] = 1.0f; */

/*   return result; */
/* } */

/* static inline mat4_t create_scale_matrix (float scale) { */
/*   mat4_t result = mat4_from_rows(Vec3{ scale,  0.0f,  0.0f }, */
/*                                  Vec3{  0.0f, scale,  0.0f }, */
/*                                  Vec3{  0.0f,  0.0f, scale }, */
/*                                  Vec3{  0.0f,  0.0f,  0.0f }); */
/*   return result; */
/* } */

/* static inline mat4_t create_translation_matrix (const float x = 0.0f, const float y = 0.0f, const float z = 0.0f) { */
/*   mat4_t result      = identity_mat4(); */
/*   result.translation = Vec4_t { x, y, z, 1.0f }; */
/*   return result; */
/* } */

template <Numeric N>
constexpr N clamp (N value, N min = static_cast<N>(0), N max = static_cast<N>(1)) {
  return min > value ? min : (value < max ? value : max);
}

/* inline float lerp (float start, float end, float gradient) { */
/*   float result = start + clamp(gradient) * (end - start); */
/*   return result; */
/* } */

static Vec2<f32> quadratic_spline (const Vec2<f32> a, const Vec2<f32> b, const Vec2<f32> control, const f32 t) {
  const Vec2<f32> point = a * square(1 - t) + control * (2 * t * (1 - t)) + b * square(t);
  return point;
}

static Vec2<f32> cubic_spline (const Vec2<f32> p0, const Vec2<f32> p3, const Vec2<f32> p1, const Vec2<f32> p2, const f32 t) {
  const auto point = p0 * cube(1.0f - t) + p1 * (3 * t * square(1.0f - t)) + p2 * (3 * square(t) * (1.0f - t)) + p3 * cube(t);
  return point;
}

} // namespace Fin
