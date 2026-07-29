execute_process(
	COMMAND "${FAIRYWRITER_PYTHON}" "${FAIRYWRITER_GFM_RUNNER}"
		--program "${FAIRYWRITER_CMARK}"
		--spec "${FAIRYWRITER_GFM_SPEC}"
	RESULT_VARIABLE gfm_result
	OUTPUT_VARIABLE gfm_output
	ERROR_VARIABLE gfm_error
)
if(NOT gfm_result EQUAL 0)
	message(FATAL_ERROR
		"Vendored GFM conformance failed.\n${gfm_output}\n${gfm_error}")
endif()
message(STATUS "Vendored GFM conformance: ${gfm_output}")

execute_process(
	COMMAND "${FAIRYWRITER_PERSISTENCE_TEST}"
	RESULT_VARIABLE persistence_result
	OUTPUT_VARIABLE persistence_output
	ERROR_VARIABLE persistence_error
)
if(NOT persistence_result EQUAL 0)
	message(FATAL_ERROR
		"FairyWriter persistence tests failed.\n"
		"${persistence_output}\n${persistence_error}")
endif()
message(STATUS "${persistence_output}")
