// Copyright 2022 Ant Group Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "heu/pylib/numpy_binding/bind_numpy.h"

#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include "heu/library/numpy/matrix.h"
#include "heu/library/numpy/numpy.h"
#include "heu/library/numpy/random.h"
#include "heu/library/numpy/toolbox.h"
#include "heu/pylib/numpy_binding/infeed.h"
#include "heu/pylib/numpy_binding/outfeed.h"
#include "heu/pylib/numpy_binding/py_slicer.h"
#include "heu/pylib/phe_binding//py_encoders.h"

namespace heu::pylib {

namespace py = ::pybind11;
namespace phe = ::heu::lib::phe;
namespace hnp = ::heu::lib::numpy;

namespace {

template <typename T>
void BindMatrixCommon(py::class_<hnp::DenseMatrix<T>>& clazz) {
  clazz.def("__str__", &hnp::DenseMatrix<T>::ToString)
      .def(PyUtils::PickleSupport<hnp::DenseMatrix<T>>())
      .def_property_readonly("rows", &hnp::DenseMatrix<T>::rows,
                             "Get the number of rows")
      .def_property_readonly("cols", &hnp::DenseMatrix<T>::cols,
                             "Get the number of cols")
      .def_property_readonly("size", &hnp::DenseMatrix<T>::size,
                             "Number of elements in the array")
      .def_property_readonly("ndim", &hnp::DenseMatrix<T>::ndim,
                             "The array's number of dimensions")
      .def_property_readonly("shape", &hnp::DenseMatrix<T>::shape,
                             "The array's shape")
      .def("__getitem__", &PySlicer<T>::GetItem, "Return self[key]")
      .def("__setitem__", &PySlicer<T>::SetItem, "Set self[key] to value");
}

template <typename T, typename... ARGS>
void BindToNumpy(py::class_<hnp::PMatrix>& clazz, ARGS&&... args) {
  clazz.def(
      "to_numpy",
      py::overload_cast<const lib::numpy::PMatrix&, const T&>(
          &DecodeNdarray<T>),
      std::forward<ARGS>(args)..., py::return_value_policy::move,
      fmt::format("Decode plaintext array to numpy ndarray with type '{}'",
                  std::string(py::str(py::dtype(T::DefaultPyTypeFormat))))
          .c_str());
}

template <typename T>
void BindArrayForModule(pybind11::module& m) {
  m.def("array", &EncodeNdarray<T>, py::arg("ndarray"), py::arg("encoder"),
        fmt::format("Create and encode an array using {}", py::type_id<T>())
            .c_str());
  m.def(
      "array", &ParseEncodeNdarray<T>, py::arg("object"), py::arg("encoder"),
      fmt::format("Encode a numpy ndarray using {}", py::type_id<T>()).c_str());
}

template <typename T, typename PY_CLASS>
void BindArrayForClass(PY_CLASS& m) {
  using EncoderT = decltype(std::declval<T&>().Instance(phe::SchemaType()));

  m.def(
      "array",
      [](const phe::HeKitPublicBase& self, const py::array& ndarray,
         const T& encoder) {
        return EncodeNdarray<EncoderT>(ndarray,
                                       encoder.Instance(self.GetSchemaType()));
      },
      py::arg("ndarray"), py::arg("encoder"),
      fmt::format("Create and encode an array using {}", py::type_id<T>())
          .c_str());
  m.def(
      "array",
      [](const phe::HeKitPublicBase& self, const py::object& ptr,
         const T& encoder) {
        return ParseEncodeNdarray<EncoderT>(
            ptr, encoder.Instance(self.GetSchemaType()));
      },
      py::arg("object"), py::arg("encoder"),
      fmt::format("Encode a numpy ndarray using {}", py::type_id<T>()).c_str());

  // bind default
  if constexpr (std::is_same_v<T, PyBigintEncoderParams>) {
    m.def(
        "array",
        [](const phe::HeKitPublicBase& self, const py::array& ndarray) {
          return EncodeNdarray<EncoderT>(ndarray,
                                         PyBigintEncoder(self.GetSchemaType()));
        },
        py::arg("ndarray"),
        "Create and encode an array using default BigintEncoder");
    m.def(
        "array",
        [](const phe::HeKitPublicBase& self, const py::object& ptr) {
          return ParseEncodeNdarray<EncoderT>(
              ptr, PyBigintEncoder(self.GetSchemaType()));
        },
        py::arg("object"),
        "Encode a numpy ndarray using default BigintEncoder");
  }
}

}  // namespace

void PyBindNumpy(pybind11::module& m) {
  py::register_local_exception<yasl::Exception>(m, "NumpyRuntimeError",
                                                PyExc_RuntimeError);
  /****** Basic types ******/
  py::class_<hnp::Shape>(m, "Shape")
      .def(py::init<std::vector<int64_t>>())
      .def(py::init([](const py::args& args) {
        return hnp::Shape(py::cast<std::vector<int64_t>>(args));
      }))
      .def("__str__", &hnp::Shape::ToString)
      .def("__repr__",
           [](const hnp::Shape& s) { return "Shape" + s.ToString(); })
      .def("__len__", [](const hnp::Shape& s) { return s.Ndim(); })
      .def(
          "__iter__",
          [](const hnp::Shape& s) {
            return py::make_iterator(s.begin(), s.end());
          },
          // Essential: keep object alive while iterator exists
          py::keep_alive<0, 1>())
      .def(PyUtils::PickleSupport<hnp::Shape>())
      .def("__getitem__", [](const hnp::Shape& s, int64_t i) { return s[i]; })
      .def("__getitem__", [](const hnp::Shape& s, const py::slice& slice) {
        size_t start = 0, stop = 0, step = 0, slicelength = 0;
        if (!slice.compute(s.Ndim(), &start, &stop, &step, &slicelength)) {
          throw py::error_already_set();
        }
        auto seq = std::vector<int64_t>(slicelength);
        for (size_t i = start; i < stop; i += step) {
          seq[i] = s[i];
        }
        return hnp::Shape(seq);
      });
  py::implicitly_convertible<std::vector<int64_t>, hnp::Shape>();

  // bind pmatrix
  auto pmatrix = py::class_<hnp::PMatrix>(m, "PlaintextArray");
  BindMatrixCommon(pmatrix);
  BindToNumpy<PyBigintDecoder>(pmatrix, py::arg("encoder") = PyBigintDecoder());
  BindToNumpy<PyIntegerEncoder>(pmatrix, py::arg("encoder"));
  BindToNumpy<PyFloatEncoder>(pmatrix, py::arg("encoder"));
  BindToNumpy<PyBatchEncoder>(pmatrix, py::arg("encoder"));
  pmatrix
      // todo: TransposeInplace impl
      //      .def("transpose_inplace", &hnp::PMatrix::TransposeInplace,
      //           "Transpose of the array")
      .def(
          "to_bytes",
          [](const hnp::PMatrix& pm, size_t bytes_per_int,
             const std::string& endian) {
            auto buf = hnp::Toolbox::PMatrixToBytes(
                pm, bytes_per_int, PyUtils::PyEndianToCpp(endian));
            return py::bytes(buf.data<char>(), buf.size());  // this is a copy
          },
          py::arg("bytes_per_int"), py::arg("endian"),
          "Construct Python bytes containing the raw data bytes in the "
          "array.\n\nThe endian argument determines the byte order used to "
          "represent an integer. To request the native byte order of the host "
          "system, use sys.byteorder as the byte order value.");

  // bind cmatrix
  auto cmatrix = py::class_<hnp::CMatrix>(m, "CiphertextArray");
  BindMatrixCommon(cmatrix);
  auto strmatrix = py::class_<hnp::DenseMatrix<std::string>>(m, "StringArray");
  BindMatrixCommon(strmatrix);

  // Bind hnp.array()
  // Usage: hnp.array([1, 2, 3], he_kit.bigint_encoder())
  BindArrayForModule<PyBigintEncoder>(m);
  BindArrayForModule<PyIntegerEncoder>(m);
  BindArrayForModule<PyFloatEncoder>(m);
  BindArrayForModule<PyBatchEncoder>(m);

  // random generator
  py::class_<hnp::Random>(m, "random")
      .def_static("randint", &hnp::Random::RandInt, py::arg("min"),
                  py::arg("max"), py::arg("shape"),
                  "Return a random integer array from the “discrete uniform” "
                  "distribution in interval [min, max)")
      .def_static("randbits", &hnp::Random::RandBits, py::arg("schema"),
                  py::arg("bits"), py::arg("shape"),
                  "Return a random integer array where each element is 'bits' "
                  "bits long");

  /****** key management ******/
  // api for sk_keeper party
  auto he_kit = py::class_<hnp::HeKit, phe::HeKitSecretBase>(m, "HeKit");
  he_kit
      .def(py::init<phe::HeKit>(),
           "Init hnp::HeKit from a created phe::HeKit context")
      .def("encryptor", &hnp::HeKit::GetEncryptor, "Get encryptor")
      .def("decryptor", &hnp::HeKit::GetDecryptor, "Get decryptor")
      .def("evaluator", &hnp::HeKit::GetEvaluator, "Get evaluator");
  BindArrayForClass<PyBigintEncoderParams>(he_kit);
  BindArrayForClass<PyIntegerEncoderParams>(he_kit);
  BindArrayForClass<PyFloatEncoderParams>(he_kit);
  BindArrayForClass<PyBatchEncoderParams>(he_kit);

  m.def(
      "setup",
      [](phe::SchemaType schema_type, size_t key_size) {
        return hnp::HeKit(phe::HeKit(schema_type, key_size));
      },
      py::arg("schema_type") = phe::SchemaType::ZPaillier,
      py::arg("key_size") = 2048, py::return_value_policy::move,
      "Setup phe (numpy) environment by schema type and key size");

  m.def(
      "setup",
      [](const std::string& schema_string, size_t key_size) -> hnp::HeKit {
        return hnp::HeKit(
            phe::HeKit(phe::ParseSchemaType(schema_string), key_size));
      },
      py::arg("schema_string") = "z-paillier", py::arg("key_size") = 2048,
      py::return_value_policy::move,
      "Setup phe (numpy) environment by schema string and key size");

  // api for evaluator party
  auto dhe_kit = py::class_<hnp::DestinationHeKit, phe::HeKitPublicBase>(
      m, "DestinationHeKit");
  dhe_kit
      .def(py::init<phe::DestinationHeKit>(),
           "Init hnp::DestinationHeKit from already created "
           "phe::DestinationHeKit context")
      .def("encryptor", &hnp::DestinationHeKit::GetEncryptor, "Get encryptor")
      .def("evaluator", &hnp::DestinationHeKit::GetEvaluator, "Get evaluator");
  BindArrayForClass<PyBigintEncoderParams>(dhe_kit);
  BindArrayForClass<PyIntegerEncoderParams>(dhe_kit);
  BindArrayForClass<PyFloatEncoderParams>(dhe_kit);
  BindArrayForClass<PyBatchEncoderParams>(dhe_kit);

  m.def(
      "setup",
      [](std::shared_ptr<phe::PublicKey> pk) {
        return hnp::DestinationHeKit(phe::DestinationHeKit(std::move(pk)));
      },
      py::arg("public_key"), py::return_value_policy::move,
      "Setup phe (numpy) environment by an already generated public key");

  /****** encryption ******/
  py::class_<hnp::Encryptor, std::shared_ptr<hnp::Encryptor>>(m, "Encryptor")
      // This is a workaround. If we use inheritance, the function of the same
      // name in parent (phe::Encryptor) will be hidden, which may be a bug of
      // pybind11
      .def_property_readonly(
          "phe", [](hnp::Encryptor& self) -> phe::Encryptor& { return self; },
          "Get the instance of phe.Encryptor for scalar encryption")
      .def("encrypt",
           py::overload_cast<const phe::Plaintext&>(&hnp::Encryptor::Encrypt,
                                                    py::const_),
           py::arg("plaintext"),
           "Encrypt plaintext (scalar) to ciphertext (scalar)")
      .def("encrypt",
           py::overload_cast<const hnp::PMatrix&>(&hnp::Encryptor::Encrypt,
                                                  py::const_),
           py::arg("plaintext_array"),
           "Encrypt plaintext array to ciphertext array")
      .def("encrypt_with_audit", &hnp::Encryptor::EncryptWithAudit,
           "Encrypt and build audit string including "
           "plaintext/random/ciphertext info");

  /****** decryption ******/
  py::class_<hnp::Decryptor, std::shared_ptr<hnp::Decryptor>>(m, "Decryptor")
      // This is a workaround. If we use inheritance, the function of the same
      // name in parent (phe::Decryptor) will be hidden, which may be a bug of
      // pybind11
      .def_property_readonly(
          "phe", [](hnp::Decryptor& self) -> phe::Decryptor& { return self; })
      .def("decrypt",
           py::overload_cast<const phe::Ciphertext&>(&hnp::Decryptor::Decrypt,
                                                     py::const_),
           py::arg("ciphertext"),
           "Decrypt ciphertext (scalar) to plaintext (scalar)")
      .def("decrypt",
           py::overload_cast<const hnp::CMatrix&>(&hnp::Decryptor::Decrypt,
                                                  py::const_),
           py::arg("ciphertext_array"),
           "Decrypt ciphertext array to plaintext array");

  /****** evaluation ******/
  py::class_<hnp::Evaluator, std::shared_ptr<hnp::Evaluator>>(m, "Evaluator")
      // This is a workaround. If we use inheritance, the function of the same
      // name in parent (phe::Evaluator) will be hidden, which may be a bug of
      // pybind11
      .def_property_readonly(
          "phe", [](hnp::Evaluator& self) -> phe::Evaluator& { return self; })
      .def("add", py::overload_cast<const hnp::CMatrix&, const hnp::CMatrix&>(
                      &hnp::Evaluator::Add, py::const_))
      .def("add", py::overload_cast<const hnp::CMatrix&, const hnp::PMatrix&>(
                      &hnp::Evaluator::Add, py::const_))
      .def("add", py::overload_cast<const hnp::PMatrix&, const hnp::CMatrix&>(
                      &hnp::Evaluator::Add, py::const_))
      .def("add", py::overload_cast<const hnp::PMatrix&, const hnp::PMatrix&>(
                      &hnp::Evaluator::Add, py::const_))

      .def("sub", py::overload_cast<const hnp::CMatrix&, const hnp::CMatrix&>(
                      &hnp::Evaluator::Sub, py::const_))
      .def("sub", py::overload_cast<const hnp::CMatrix&, const hnp::PMatrix&>(
                      &hnp::Evaluator::Sub, py::const_))
      .def("sub", py::overload_cast<const hnp::PMatrix&, const hnp::CMatrix&>(
                      &hnp::Evaluator::Sub, py::const_))
      .def("sub", py::overload_cast<const hnp::PMatrix&, const hnp::PMatrix&>(
                      &hnp::Evaluator::Sub, py::const_))

      .def("mul", py::overload_cast<const hnp::CMatrix&, const hnp::PMatrix&>(
                      &hnp::Evaluator::Mul, py::const_))
      .def("mul", py::overload_cast<const hnp::PMatrix&, const hnp::CMatrix&>(
                      &hnp::Evaluator::Mul, py::const_))
      .def("mul", py::overload_cast<const hnp::PMatrix&, const hnp::PMatrix&>(
                      &hnp::Evaluator::Mul, py::const_))

      .def("matmul", &hnp::Evaluator::MatMul<phe::Ciphertext, phe::Plaintext>)
      .def("matmul", &hnp::Evaluator::MatMul<phe::Plaintext, phe::Ciphertext>)
      .def("matmul", &hnp::Evaluator::MatMul<phe::Plaintext, phe::Plaintext>)

      .def("sum", &hnp::Evaluator::Sum<phe::Plaintext>)
      .def("sum", &hnp::Evaluator::Sum<phe::Ciphertext>);
}

}  // namespace heu::pylib
